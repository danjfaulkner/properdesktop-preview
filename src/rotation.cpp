// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "rotation.h"

#include "base/file.h"

#include <png.h>

#include <algorithm>
#include <cerrno>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace properdesktop;
namespace fs = std::filesystem;

namespace rotation {

static constexpr uint16_t kOrientationTag = 0x0112;
static constexpr uint16_t kShortType = 3;

bool turnsType(const std::string& mimeType)
{
    return mimeType == "image/jpeg" || mimeType == "image/png";
}

// The file a path names, through any symbolic links: that is what is
// replaced, in its own folder.
static std::string realFile(const std::string& path)
{
    std::error_code error;
    const fs::path real = fs::canonical(path, error);
    return error ? path : real.string();
}

bool canTurn(const std::string& path, const std::string& mimeType)
{
    const std::string file = realFile(path);
    const std::string folder = fs::path(file).parent_path().string();
    return turnsType(mimeType) && ::access(file.c_str(), W_OK) == 0 && ::access(folder.c_str(), W_OK | X_OK) == 0;
}

int turnedOrientation(int orientation, int quarterTurns)
{
    // One quarter clockwise more: the upright ones go 1, 6, 3, 8 and the
    // mirrored ones 2, 7, 4, 5.
    static constexpr int kNext[] = {1, 6, 7, 8, 5, 2, 3, 4, 1};
    if (orientation < 1 || orientation > 8)
        orientation = 1;
    for (int i = 0; i < ((quarterTurns % 4) + 4) % 4; ++i)
        orientation = kNext[orientation];
    return orientation;
}

// Replaces the file with these bytes through a new file in its folder,
// renamed over it, with its permissions.
static int replaceFile(const std::string& path, const std::string& bytes)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0)
        return errno;
    const fs::path file(path);
    std::string temporary = (file.parent_path() / ("." + file.filename().string() + ".XXXXXX")).string();
    const int fd = ::mkstemp(temporary.data());
    if (fd < 0)
        return errno;
    int error = ::fchmod(fd, st.st_mode & 07777) == 0 ? 0 : errno;
    for (size_t written = 0; !error && written < bytes.size();) {
        const ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0 && errno != EINTR)
            error = errno;
        else if (n > 0)
            written += size_t(n);
    }
    if (!error && ::fsync(fd) != 0)
        error = errno;
    if (::close(fd) != 0 && !error)
        error = errno;
    if (!error && ::rename(temporary.c_str(), path.c_str()) != 0)
        error = errno;
    if (error)
        ::unlink(temporary.c_str());
    return error;
}

// Byte order of the TIFF structure inside an Exif segment.
struct Tiff
{
    std::string& data;
    size_t start;
    bool little;

    uint32_t get(size_t at, int size) const
    {
        uint32_t value = 0;
        for (int i = 0; i < size; ++i) {
            const uint32_t byte = uint8_t(data[start + at + size_t(i)]);
            value |= little ? byte << (8 * i) : byte << (8 * (size - 1 - i));
        }
        return value;
    }

    void put(size_t at, int size, uint32_t value)
    {
        for (int i = 0; i < size; ++i)
            data[start + at + size_t(i)] = char(little ? value >> (8 * i) : value >> (8 * (size - 1 - i)));
    }
};

// The APP1 segment with Exif data and the orientation alone.
static std::string exifSegment(int orientation)
{
    std::string body("Exif\0\0II*\0\x08\0\0\0\x01\0", 16);
    body += std::string("\x12\x01\x03\0\x01\0\0\0", 8);
    body += char(orientation);
    body += std::string(3, '\0');
    body += std::string(4, '\0');
    const size_t length = body.size() + 2;
    return std::string("\xff\xe1", 2) + char(length >> 8) + char(length & 0xff) + body;
}

// Sets the orientation in the Exif segment at `segment` (its marker), or adds
// it: IFD0 is copied with the tag to the segment's end and the header points
// there, so nothing else moves and every offset in it stays right.
static bool setExifOrientation(std::string& data, size_t segment, int quarterTurns)
{
    const size_t length = uint8_t(data[segment + 2]) << 8 | uint8_t(data[segment + 3]);
    const size_t end = segment + 2 + length;
    const size_t start = segment + 10;
    if (end > data.size() || length < 8 + 8 || (data.compare(start, 2, "II") != 0 && data.compare(start, 2, "MM") != 0))
        return false;
    Tiff tiff {data, start, data[start] == 'I'};
    const size_t size = end - start;
    const size_t ifd = tiff.get(4, 4);
    if (ifd + 2 > size)
        return false;
    const uint32_t count = tiff.get(ifd, 2);
    if (ifd + 2 + count * 12 + 4 > size)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t entry = ifd + 2 + i * 12;
        if (tiff.get(entry, 2) == kOrientationTag) {
            tiff.put(entry + 8, 2, uint32_t(turnedOrientation(int(tiff.get(entry + 8, 2)), quarterTurns)));
            return true;
        }
    }
    // Offsets in TIFF are even.
    const size_t copy = size + size % 2;
    const size_t grown = copy + 2 + (count + 1) * 12 + 4;
    if (length + grown - size > 0xffff)
        return false;
    data.insert(end, std::string(grown - size, '\0'));
    Tiff out {data, start, tiff.little};
    out.put(copy, 2, count + 1);
    size_t at = copy + 2;
    bool placed = false;
    for (uint32_t i = 0; i <= count; ++i) {
        const size_t entry = ifd + 2 + i * 12;
        if (!placed && (i == count || tiff.get(entry, 2) > kOrientationTag)) {
            out.put(at, 2, kOrientationTag);
            out.put(at + 2, 2, kShortType);
            out.put(at + 4, 4, 1);
            out.put(at + 8, 4, 0);
            out.put(at + 8, 2, uint32_t(turnedOrientation(1, quarterTurns)));
            at += 12;
            placed = true;
        }
        if (i == count)
            break;
        data.replace(start + at, 12, data, start + entry, 12);
        at += 12;
    }
    out.put(at, 4, tiff.get(ifd + 2 + count * 12, 4));
    out.put(4, 4, uint32_t(copy));
    const size_t newLength = length + (grown - size);
    data[segment + 2] = char(newLength >> 8);
    data[segment + 3] = char(newLength & 0xff);
    return true;
}

// Rewrites the orientation in the segments before the scan; the scan is
// left as it is.
static std::optional<std::string> turnedJpeg(std::string data, int quarterTurns)
{
    if (data.size() < 4 || uint8_t(data[0]) != 0xff || uint8_t(data[1]) != 0xd8)
        return std::nullopt;
    size_t offset = 2;
    size_t afterApp0 = 2;
    while (offset + 4 <= data.size() && uint8_t(data[offset]) == 0xff) {
        const uint8_t marker = uint8_t(data[offset + 1]);
        if (marker == 0xff) {
            ++offset;
            continue;
        }
        if (marker == 0xda || marker == 0xd9)
            break;
        const size_t length = uint8_t(data[offset + 2]) << 8 | uint8_t(data[offset + 3]);
        if (marker == 0xe1 && data.compare(offset + 4, 6, std::string("Exif\0\0", 6)) == 0)
            return setExifOrientation(data, offset, quarterTurns) ? std::optional(std::move(data)) : std::nullopt;
        if (marker == 0xe0 && offset == 2)
            afterApp0 = offset + 2 + length;
        offset += 2 + length;
    }
    // No Exif yet: after the JFIF segment, which has to come first.
    data.insert(afterApp0, exifSegment(turnedOrientation(1, quarterTurns)));
    return data;
}

struct PngReader
{
    const std::string& data;
    size_t offset = 8;
};

static void readPngData(png_structp png, png_bytep out, png_size_t length)
{
    auto* reader = static_cast<PngReader*>(png_get_io_ptr(png));
    if (reader->offset + length > reader->data.size())
        png_error(png, "truncated");
    std::memcpy(out, reader->data.data() + reader->offset, length);
    reader->offset += length;
}

static void writePngData(png_structp png, png_bytep in, png_size_t length)
{
    static_cast<std::string*>(png_get_io_ptr(png))->append(reinterpret_cast<const char*>(in), length);
}

static void flushPng(png_structp)
{
}

// The pixels as libpng gives them, whole bytes per pixel, turned.
static std::vector<png_byte> turnedPixels(const std::vector<png_byte>& pixels, uint32_t width, uint32_t height,
                                          size_t bytes, int turns)
{
    std::vector<png_byte> out(pixels.size());
    const uint32_t outWidth = turns % 2 ? height : width;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t tx = x;
            uint32_t ty = y;
            if (turns == 1) {
                tx = height - 1 - y;
                ty = x;
            } else if (turns == 2) {
                tx = width - 1 - x;
                ty = height - 1 - y;
            } else if (turns == 3) {
                tx = y;
                ty = width - 1 - x;
            }
            std::memcpy(&out[(size_t(ty) * outWidth + tx) * bytes], &pixels[(size_t(y) * width + x) * bytes], bytes);
        }
    }
    return out;
}

// Unknown chunks whose fourth letter is upper case are not safe to copy
// once the image data changes (such as iDOT, offsets into the old IDAT);
// the colour description ones do not depend on the pixels' places, and
// stay when this libpng does not know them.
static bool keepsUnknown(const png_unknown_chunk& chunk)
{
    const bool safeToCopy = chunk.name[3] & 0x20;
    for (const char* name : {"cICP", "mDCV", "cLLI"}) {
        if (std::memcmp(chunk.name, name, 4) == 0)
            return true;
    }
    return safeToCopy && std::memcmp(chunk.name, "acTL", 4) != 0;
}

// The Exif orientation set to 1: the turned pixels are what is shown, as
// the decoder does not read a PNG's eXIf; a reader that does then agrees.
static std::string uprightExif(const png_bytep exif, png_uint_32 length)
{
    std::string data(reinterpret_cast<const char*>(exif), length);
    if (data.size() < 8 || (data.compare(0, 2, "II") != 0 && data.compare(0, 2, "MM") != 0))
        return data;
    Tiff tiff {data, 0, data[0] == 'I'};
    const size_t ifd = tiff.get(4, 4);
    if (ifd + 2 > data.size())
        return data;
    const uint32_t count = tiff.get(ifd, 2);
    for (uint32_t i = 0; i < count && ifd + 2 + (i + 1) * 12 <= data.size(); ++i) {
        const size_t entry = ifd + 2 + i * 12;
        if (tiff.get(entry, 2) == kOrientationTag)
            tiff.put(entry + 8, 2, 1);
    }
    return data;
}

// Copies the chunks that describe the pixels or the file, from before or
// after IDAT, all to be written before it. Left out: oFFs, a position on a
// page that a turn invalidates, and the unsafe unknown chunks above.
static void copyChunks(png_structp read, png_infop info, png_structp write, png_infop out, bool swapsAxes)
{
    png_colorp palette = nullptr;
    int paletteSize = 0;
    if (png_get_PLTE(read, info, &palette, &paletteSize))
        png_set_PLTE(write, out, palette, paletteSize);
    png_bytep alpha = nullptr;
    int alphaCount = 0;
    png_color_16p transparent = nullptr;
    if (png_get_tRNS(read, info, &alpha, &alphaCount, &transparent))
        png_set_tRNS(write, out, alpha, alphaCount, transparent);
    png_color_16p background = nullptr;
    if (png_get_bKGD(read, info, &background))
        png_set_bKGD(write, out, background);
    png_uint_16p histogram = nullptr;
    if (png_get_hIST(read, info, &histogram))
        png_set_hIST(write, out, histogram);
    png_fixed_point gamma = 0;
    if (png_get_gAMA_fixed(read, info, &gamma))
        png_set_gAMA_fixed(write, out, gamma);
    png_fixed_point whiteX, whiteY, redX, redY, greenX, greenY, blueX, blueY;
    if (png_get_cHRM_fixed(read, info, &whiteX, &whiteY, &redX, &redY, &greenX, &greenY, &blueX, &blueY))
        png_set_cHRM_fixed(write, out, whiteX, whiteY, redX, redY, greenX, greenY, blueX, blueY);
    int intent = 0;
    if (png_get_sRGB(read, info, &intent))
        png_set_sRGB(write, out, intent);
    png_charp profileName = nullptr;
    int compression = 0;
    png_bytep profile = nullptr;
    png_uint_32 profileLength = 0;
    if (png_get_iCCP(read, info, &profileName, &compression, &profile, &profileLength))
        png_set_iCCP(write, out, profileName, compression, profile, profileLength);
#ifdef PNG_cICP_SUPPORTED
    png_byte primaries = 0, transfer = 0, matrix = 0, fullRange = 0;
    if (png_get_cICP(read, info, &primaries, &transfer, &matrix, &fullRange))
        png_set_cICP(write, out, primaries, transfer, matrix, fullRange);
#endif
#ifdef PNG_mDCV_SUPPORTED
    png_fixed_point mWhiteX, mWhiteY, mRedX, mRedY, mGreenX, mGreenY, mBlueX, mBlueY;
    png_uint_32 maxLuminance = 0, minLuminance = 0;
    if (png_get_mDCV_fixed(read, info, &mWhiteX, &mWhiteY, &mRedX, &mRedY, &mGreenX, &mGreenY, &mBlueX, &mBlueY,
                           &maxLuminance, &minLuminance))
        png_set_mDCV_fixed(write, out, mWhiteX, mWhiteY, mRedX, mRedY, mGreenX, mGreenY, mBlueX, mBlueY,
                           maxLuminance, minLuminance);
#endif
#ifdef PNG_cLLI_SUPPORTED
    png_uint_32 maxContent = 0, maxFrame = 0;
    if (png_get_cLLI_fixed(read, info, &maxContent, &maxFrame))
        png_set_cLLI_fixed(write, out, maxContent, maxFrame);
#endif
    png_color_8p significant = nullptr;
    if (png_get_sBIT(read, info, &significant))
        png_set_sBIT(write, out, significant);
    png_uint_32 resX = 0, resY = 0;
    int unit = 0;
    if (png_get_pHYs(read, info, &resX, &resY, &unit))
        png_set_pHYs(write, out, swapsAxes ? resY : resX, swapsAxes ? resX : resY, unit);
    png_charp scaleWidth = nullptr;
    png_charp scaleHeight = nullptr;
    if (png_get_sCAL_s(read, info, &unit, &scaleWidth, &scaleHeight))
        png_set_sCAL_s(write, out, unit, swapsAxes ? scaleHeight : scaleWidth, swapsAxes ? scaleWidth : scaleHeight);
    png_charp purpose = nullptr;
    png_int_32 x0 = 0, x1 = 0;
    int type = 0, parameterCount = 0;
    png_charp units = nullptr;
    png_charpp parameters = nullptr;
    if (png_get_pCAL(read, info, &purpose, &x0, &x1, &type, &parameterCount, &units, &parameters))
        png_set_pCAL(write, out, purpose, x0, x1, type, parameterCount, units, parameters);
    png_sPLT_tp palettes = nullptr;
    const int paletteCount = png_get_sPLT(read, info, &palettes);
    if (paletteCount > 0)
        png_set_sPLT(write, out, palettes, paletteCount);
    png_timep time = nullptr;
    if (png_get_tIME(read, info, &time))
        png_set_tIME(write, out, time);
    png_uint_32 exifLength = 0;
    png_bytep exif = nullptr;
    if (png_get_eXIf_1(read, info, &exifLength, &exif)) {
        std::string upright = uprightExif(exif, exifLength);
        png_set_eXIf_1(write, out, png_uint_32(upright.size()), reinterpret_cast<png_bytep>(upright.data()));
    }
    png_textp text = nullptr;
    int textCount = 0;
    if (png_get_text(read, info, &text, &textCount) > 0)
        png_set_text(write, out, text, textCount);
    png_unknown_chunkp unknown = nullptr;
    const int unknownCount = png_get_unknown_chunks(read, info, &unknown);
    for (int i = 0; i < unknownCount; ++i) {
        if (!keepsUnknown(unknown[i]))
            continue;
        png_unknown_chunk chunk = unknown[i];
        chunk.location = PNG_HAVE_PLTE;
        png_set_unknown_chunks(write, out, &chunk, 1);
    }
}

// Decodes to whole bytes per pixel at the file's depth and encodes the
// turned pixels with the same header and chunks.
static std::optional<std::string> turnedPng(const std::string& data, int quarterTurns)
{
    if (data.size() < 8 || png_sig_cmp(reinterpret_cast<png_const_bytep>(data.data()), 0, 8) != 0)
        return std::nullopt;
    png_structp read = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(read);
    png_infop endInfo = png_create_info_struct(read);
    png_structp write = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop out = png_create_info_struct(write);
    // libpng reports errors by longjmp, past anything made after setjmp, so
    // all of it is made before.
    std::string encoded;
    std::vector<png_byte> pixels;
    std::vector<png_bytep> rows;
    volatile bool done = false;
    PngReader reader {data};
    if (setjmp(png_jmpbuf(read)) == 0) {
        if (setjmp(png_jmpbuf(write)) == 0) {
            png_set_read_fn(read, &reader, readPngData);
            png_set_sig_bytes(read, 8);
            png_set_keep_unknown_chunks(read, PNG_HANDLE_CHUNK_ALWAYS, nullptr, 0);
            png_read_info(read, info);
            png_uint_32 width = 0, height = 0;
            int depth = 0, colour = 0, interlace = 0;
            png_get_IHDR(read, info, &width, &height, &depth, &colour, &interlace, nullptr, nullptr);
            png_unknown_chunkp unknown = nullptr;
            const int unknownCount = png_get_unknown_chunks(read, info, &unknown);
            const bool animated = std::any_of(unknown, unknown + std::max(0, unknownCount), [](const png_unknown_chunk& chunk) {
                return std::memcmp(chunk.name, "acTL", 4) == 0;
            });
            if (!animated) {
                if (depth < 8)
                    png_set_packing(read);
                png_set_interlace_handling(read);
                png_read_update_info(read, info);
                const size_t rowBytes = png_get_rowbytes(read, info);
                const size_t bytes = rowBytes / width;
                pixels.resize(rowBytes * height);
                rows.resize(height);
                for (png_uint_32 y = 0; y < height; ++y)
                    rows[y] = &pixels[y * rowBytes];
                png_read_image(read, rows.data());
                png_read_end(read, endInfo);

                const int turns = ((quarterTurns % 4) + 4) % 4;
                pixels = turnedPixels(pixels, width, height, bytes, turns);
                const png_uint_32 outWidth = turns % 2 ? height : width;
                const png_uint_32 outHeight = turns % 2 ? width : height;
                png_set_write_fn(write, &encoded, writePngData, flushPng);
                png_set_IHDR(write, out, outWidth, outHeight, depth, colour, interlace, PNG_COMPRESSION_TYPE_DEFAULT,
                             PNG_FILTER_TYPE_DEFAULT);
                png_set_keep_unknown_chunks(write, PNG_HANDLE_CHUNK_ALWAYS, nullptr, 0);
                copyChunks(read, info, write, out, turns % 2);
                copyChunks(read, endInfo, write, out, turns % 2);
                png_write_info(write, out);
                if (depth < 8)
                    png_set_packing(write);
                png_set_interlace_handling(write);
                const size_t outRowBytes = size_t(outWidth) * bytes;
                rows.resize(outHeight);
                for (png_uint_32 y = 0; y < outHeight; ++y)
                    rows[y] = &pixels[y * outRowBytes];
                png_write_image(write, rows.data());
                png_write_end(write, nullptr);
                done = true;
            }
        }
    }
    png_destroy_read_struct(&read, &info, &endInfo);
    png_destroy_write_struct(&write, &out);
    return done ? std::optional(std::move(encoded)) : std::nullopt;
}

int turn(const std::string& path, const std::string& mimeType, int quarterTurns)
{
    if (((quarterTurns % 4) + 4) % 4 == 0)
        return 0;
    const std::string file = realFile(path);
    std::optional<std::string> data = base::file::read(file);
    if (!data)
        return ::access(file.c_str(), R_OK) != 0 ? errno : EIO;
    const std::optional<std::string> turned = mimeType == "image/jpeg" ? turnedJpeg(std::move(*data), quarterTurns)
            : mimeType == "image/png"                                    ? turnedPng(*data, quarterTurns)
                                                                         : std::nullopt;
    if (!turned)
        return kUnreadable;
    return replaceFile(file, *turned);
}

} // namespace rotation
