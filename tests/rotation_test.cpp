// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "rotation.h"
#include "testutil.h"

#include "base/file.h"
#include "ui/image.h"
#include "ui/image_info.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <jpeglib.h>
#include <png.h>
#include <sys/stat.h>

using properdesktop::ui::Color;
using properdesktop::ui::Image;
using properdesktop::ui::ImageInfo;

// 16 x 8: red on the left half, blue on the right, a green top-left quarter
// of the red, so every turn and mirror tells apart.
static Color pattern(int x, int y)
{
    if (x < 4 && y < 4)
        return Color::rgb(0x00FF00);
    return Color::rgb(x < 8 ? 0xFF0000 : 0x0000FF);
}

// With a JFIF segment first, and an APP1 segment, when asked.
static std::string writeJpeg(const std::string& path, bool jfif = false, const std::string& app1 = {})
{
    FILE* file = std::fopen(path.c_str(), "wb");
    jpeg_compress_struct info;
    jpeg_error_mgr error;
    info.err = jpeg_std_error(&error);
    jpeg_create_compress(&info);
    jpeg_stdio_dest(&info, file);
    info.image_width = 16;
    info.image_height = 8;
    info.input_components = 3;
    info.in_color_space = JCS_RGB;
    jpeg_set_defaults(&info);
    info.write_JFIF_header = jfif;
    jpeg_set_quality(&info, 100, TRUE);
    // No chroma subsampling, so colours stay apart at their edges.
    for (int i = 0; i < info.num_components; ++i) {
        info.comp_info[i].h_samp_factor = 1;
        info.comp_info[i].v_samp_factor = 1;
    }
    jpeg_start_compress(&info, TRUE);
    if (!app1.empty())
        jpeg_write_marker(&info, JPEG_APP0 + 1, reinterpret_cast<const JOCTET*>(app1.data()), unsigned(app1.size()));
    std::vector<JSAMPLE> row(16 * 3);
    while (info.next_scanline < 8) {
        for (int x = 0; x < 16; ++x) {
            const Color c = pattern(x, int(info.next_scanline));
            row[size_t(x) * 3] = c.r;
            row[size_t(x) * 3 + 1] = c.g;
            row[size_t(x) * 3 + 2] = c.b;
        }
        JSAMPROW pointer = row.data();
        jpeg_write_scanlines(&info, &pointer, 1);
    }
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);
    std::fclose(file);
    return path;
}

// The entropy-coded scan and what follows it.
static std::string scanOf(const std::string& data)
{
    const size_t sos = data.find("\xff\xda");
    return sos == std::string::npos ? std::string() : data.substr(sos);
}

static bool near(Color a, Color b)
{
    return std::abs(a.r - b.r) < 40 && std::abs(a.g - b.g) < 40 && std::abs(a.b - b.b) < 40;
}

// The decoded picture is the pattern turned this many quarters clockwise.
static void expectTurned(const std::string& path, int turns)
{
    const Image image = Image::fromFile(path);
    ASSERT_FALSE(image.isNull());
    const bool odd = turns % 2;
    ASSERT_EQ(image.width(), odd ? 8 : 16);
    ASSERT_EQ(image.height(), odd ? 16 : 8);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            // Back to where the pixel was before each turn: one clockwise
            // turn puts (x, y) of a picture `height` tall at (height - 1 - y, x).
            int sx = x;
            int sy = y;
            int width = image.width();
            for (int i = 0; i < turns; ++i) {
                const int before = sx;
                sx = sy;
                sy = width - 1 - before;
                width = width == 8 ? 16 : 8;
            }
            // Pixels next to an edge of colour blur in a JPEG.
            const bool edge = (sx % 4 == 3 || sx % 4 == 0) || (sy % 4 == 3 || sy % 4 == 0);
            if (!edge)
                EXPECT_TRUE(near(image.pixel(x, y), pattern(sx, sy))) << path << " " << turns << " at " << x << "," << y;
        }
    }
}

TEST(RotationTest, OrientationsTurnInFour)
{
    for (int orientation = 1; orientation <= 8; ++orientation) {
        EXPECT_EQ(rotation::turnedOrientation(orientation, 4), orientation);
        EXPECT_EQ(rotation::turnedOrientation(orientation, -1), rotation::turnedOrientation(orientation, 3));
    }
    EXPECT_EQ(rotation::turnedOrientation(1, 1), 6);
    EXPECT_EQ(rotation::turnedOrientation(1, 2), 3);
    EXPECT_EQ(rotation::turnedOrientation(1, -1), 8);
}

TEST(RotationTest, AJpegTurnsByItsOrientationAlone)
{
    TemporaryDir directory;
    for (const bool jfif : {false, true}) {
        const std::string path = writeJpeg(directory.filePath(jfif ? "jfif.jpg" : "plain.jpg"), jfif);
        const std::string original = *properdesktop::base::file::read(path);
        for (int turns = 1; turns <= 4; ++turns) {
            ASSERT_EQ(rotation::turn(path, "image/jpeg", 1), 0);
            const std::string turned = *properdesktop::base::file::read(path);
            EXPECT_EQ(scanOf(turned), scanOf(original));
            if (jfif)
                EXPECT_EQ(turned.compare(2, 2, "\xff\xe0"), 0);
            const ImageInfo info = properdesktop::ui::readImageInfo(path, "image/jpeg");
            EXPECT_EQ(info.width, turns % 2 ? 8 : 16);
            expectTurned(path, turns % 4);
        }
    }
}

TEST(RotationTest, AnExifWithoutOrientationGetsOne)
{
    TemporaryDir directory;
    // Little-endian TIFF, IFD0 at 8 with one entry: Make, ASCII, 9 bytes at 26.
    std::string exif("Exif\0\0II*\0\x08\0\0\0\x01\0", 16);
    exif += std::string("\x0f\x01\x02\0\x09\0\0\0\x1a\0\0\0", 12);
    exif += std::string(4, '\0');
    exif += std::string("ProperDesktop\0", 9);
    const std::string path = writeJpeg(directory.filePath("make.jpg"), false, exif);
    const std::string original = *properdesktop::base::file::read(path);
    ASSERT_EQ(rotation::turn(path, "image/jpeg", -1), 0);
    const std::string turned = *properdesktop::base::file::read(path);
    EXPECT_EQ(scanOf(turned), scanOf(original));
    EXPECT_NE(turned.find("ProperDesktop"), std::string::npos);
    expectTurned(path, 3);
    ASSERT_EQ(rotation::turn(path, "image/jpeg", 1), 0);
    expectTurned(path, 0);
}

struct PngPixels
{
    uint32_t width = 0;
    uint32_t height = 0;
    int depth = 0;
    std::vector<uint16_t> grey;
    uint32_t resX = 0;
    uint32_t resY = 0;
    std::string text;
    bool srgb = false;
};

// A 16-bit grey 3 x 2 PNG of the values 0 to 5 times 1000, with a
// resolution, a text and an sRGB chunk.
static std::string writeGreyPng(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, file);
    png_set_IHDR(png, info, 3, 2, 16, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_set_pHYs(png, info, 100, 200, PNG_RESOLUTION_METER);
    png_set_sRGB(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    png_text text {};
    text.compression = PNG_TEXT_COMPRESSION_NONE;
    text.key = const_cast<char*>("Comment");
    text.text = const_cast<char*>("kept");
    png_set_text(png, info, &text, 1);
    png_write_info(png, info);
    for (int y = 0; y < 2; ++y) {
        png_byte row[6];
        for (int x = 0; x < 3; ++x) {
            const int value = (y * 3 + x) * 1000;
            row[x * 2] = png_byte(value >> 8);
            row[x * 2 + 1] = png_byte(value & 0xff);
        }
        png_write_row(png, row);
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return path;
}

static PngPixels readGreyPng(const std::string& path)
{
    PngPixels result;
    FILE* file = std::fopen(path.c_str(), "rb");
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, file);
    png_read_info(png, info);
    int colour = 0;
    png_get_IHDR(png, info, &result.width, &result.height, &result.depth, &colour, nullptr, nullptr, nullptr);
    int unit = 0;
    png_get_pHYs(png, info, &result.resX, &result.resY, &unit);
    int intent = 0;
    result.srgb = png_get_sRGB(png, info, &intent) != 0;
    std::vector<png_byte> row(png_get_rowbytes(png, info));
    for (uint32_t y = 0; y < result.height; ++y) {
        png_read_row(png, row.data(), nullptr);
        for (uint32_t x = 0; x < result.width; ++x)
            result.grey.push_back(uint16_t(row[x * 2] << 8 | row[x * 2 + 1]));
    }
    png_read_end(png, info);
    png_textp text = nullptr;
    int count = 0;
    if (png_get_text(png, info, &text, &count) > 0)
        result.text = text[0].text;
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);
    return result;
}

TEST(RotationTest, APngIsEncodedAgainTurnedWithItsDepthAndChunks)
{
    TemporaryDir directory;
    const std::string path = writeGreyPng(directory.filePath("grey.png"));
    ::chmod(path.c_str(), 0640);
    ASSERT_EQ(rotation::turn(path, "image/png", 1), 0);
    const PngPixels turned = readGreyPng(path);
    EXPECT_EQ(turned.width, 2u);
    EXPECT_EQ(turned.height, 3u);
    EXPECT_EQ(turned.depth, 16);
    // 0 1 2 / 3 4 5 turned clockwise: 3 0 / 4 1 / 5 2.
    EXPECT_EQ(turned.grey, (std::vector<uint16_t> {3000, 0, 4000, 1000, 5000, 2000}));
    EXPECT_EQ(turned.resX, 200u);
    EXPECT_EQ(turned.resY, 100u);
    EXPECT_TRUE(turned.srgb);
    EXPECT_EQ(turned.text, "kept");
    struct stat st;
    ::stat(path.c_str(), &st);
    EXPECT_EQ(st.st_mode & 0777, 0640u);

    for (int i = 0; i < 3; ++i)
        ASSERT_EQ(rotation::turn(path, "image/png", 1), 0);
    EXPECT_EQ(readGreyPng(path).grey, (std::vector<uint16_t> {0, 1000, 2000, 3000, 4000, 5000}));
}

TEST(RotationTest, OnlyWritableJpegAndPngFilesTurn)
{
    TemporaryDir directory;
    const std::string jpeg = writeJpeg(directory.filePath("a.jpg"));
    EXPECT_TRUE(rotation::canTurn(jpeg, "image/jpeg"));
    EXPECT_FALSE(rotation::canTurn(jpeg, "image/webp"));
    EXPECT_FALSE(rotation::canTurn(jpeg, "image/gif"));
    ::chmod(jpeg.c_str(), 0444);
    EXPECT_FALSE(rotation::canTurn(jpeg, "image/jpeg"));
    EXPECT_EQ(rotation::turn(writeFile(directory.filePath("b.png"), "not a png"), "image/png", 1),
              rotation::kUnreadable);
}

// What an 8-bit RGB 4 x 2 PNG of the values 0 to 7 carries besides pixels.
struct Extras
{
    bool iDot = false;
    bool cicp = false;
    // An eXIf after IDAT with this orientation, when not 0.
    int trailingOrientation = 0;
};

static std::string writeRgbPng(const std::string& path, const Extras& extras)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, file);
    png_set_IHDR(png, info, 4, 2, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    if (extras.cicp)
        png_set_cICP(png, info, 12, 13, 0, 1);
    png_unknown_chunk dot {};
    if (extras.iDot) {
        std::memcpy(dot.name, "iDOT", 5);
        static png_byte offsets[28] = {};
        dot.data = offsets;
        dot.size = sizeof(offsets);
        dot.location = PNG_HAVE_IHDR;
        png_set_keep_unknown_chunks(png, PNG_HANDLE_CHUNK_ALWAYS, nullptr, 0);
        png_set_unknown_chunks(png, info, &dot, 1);
    }
    png_write_info(png, info);
    for (int y = 0; y < 2; ++y) {
        png_byte row[12];
        for (int x = 0; x < 4; ++x)
            row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = png_byte((y * 4 + x) * 30);
        png_write_row(png, row);
    }
    // Little-endian TIFF, IFD0 at 8 with the orientation alone.
    std::string exif("II*\0\x08\0\0\0\x01\0\x12\x01\x03\0\x01\0\0\0", 18);
    exif += char(extras.trailingOrientation);
    exif += std::string(7, '\0');
    if (extras.trailingOrientation)
        png_set_eXIf_1(png, info, png_uint_32(exif.size()), reinterpret_cast<png_bytep>(exif.data()));
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    std::fclose(file);
    return path;
}

// The chunk names in file order.
static std::vector<std::string> chunkNames(const std::string& data)
{
    std::vector<std::string> names;
    for (size_t offset = 8; offset + 8 <= data.size();) {
        const size_t length = size_t(uint8_t(data[offset])) << 24 | size_t(uint8_t(data[offset + 1])) << 16
                | size_t(uint8_t(data[offset + 2])) << 8 | uint8_t(data[offset + 3]);
        names.push_back(data.substr(offset + 4, 4));
        offset += 12 + length;
    }
    return names;
}

static bool has(const std::vector<std::string>& names, const char* name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

TEST(RotationTest, UnsafeChunksGoAndColourOnesStay)
{
    TemporaryDir directory;
    const std::string path = writeRgbPng(directory.filePath("shot.png"), {true, true, 0});
    ASSERT_TRUE(has(chunkNames(*properdesktop::base::file::read(path)), "iDOT"));
    ASSERT_EQ(rotation::turn(path, "image/png", 1), 0);
    const std::vector<std::string> names = chunkNames(*properdesktop::base::file::read(path));
    EXPECT_FALSE(has(names, "iDOT"));
    EXPECT_TRUE(has(names, "cICP"));
}

// The decoder does not read a PNG's eXIf: what shows is the pixels, so the
// turn turns them and sets the orientation upright for readers that do.
TEST(RotationTest, AnExifAfterTheDataStaysAndIsSetUpright)
{
    TemporaryDir directory;
    const std::string path = writeRgbPng(directory.filePath("exif.png"), {false, false, 2});
    const std::vector<std::string> before = chunkNames(*properdesktop::base::file::read(path));
    ASSERT_GT(std::find(before.begin(), before.end(), "eXIf"), std::find(before.begin(), before.end(), "IDAT"));
    const Image shown = Image::fromFile(path);
    ASSERT_EQ(shown.width(), 4);
    ASSERT_EQ(rotation::turn(path, "image/png", 1), 0);

    const std::string turned = *properdesktop::base::file::read(path);
    EXPECT_TRUE(has(chunkNames(turned), "eXIf"));
    const size_t exif = turned.find("eXIf") + 4;
    // The orientation's value, 18 bytes into the TIFF data.
    EXPECT_EQ(uint8_t(turned[exif + 18]), 1);
    const Image after = Image::fromFile(path);
    ASSERT_EQ(after.width(), 2);
    ASSERT_EQ(after.height(), 4);
    // Turned clockwise: what was at (x, y) in a picture 2 tall is at (1 - y, x).
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            EXPECT_EQ(after.pixel(1 - y, x), shown.pixel(x, y));
}

TEST(RotationTest, ALinkStaysAndItsFileTurns)
{
    TemporaryDir directory;
    std::filesystem::create_directories(directory.filePath("real"));
    const std::string real = writeGreyPng(directory.filePath("real/grey.png"));
    const std::string link = directory.filePath("link.png");
    std::filesystem::create_symlink(real, link);
    EXPECT_TRUE(rotation::canTurn(link, "image/png"));
    ASSERT_EQ(rotation::turn(link, "image/png", 1), 0);
    EXPECT_TRUE(std::filesystem::is_symlink(link));
    EXPECT_EQ(readGreyPng(real).width, 2u);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory.filePath("real")),
                            std::filesystem::directory_iterator()),
              1);

    // A read-only folder of the file, behind a writable one of the link.
    ::chmod(directory.filePath("real").c_str(), 0555);
    EXPECT_FALSE(rotation::canTurn(link, "image/png"));
    ::chmod(directory.filePath("real").c_str(), 0755);
}
