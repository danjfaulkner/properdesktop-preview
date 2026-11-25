// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ui/image.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// A folder of its own under /tmp, removed with everything in it.
class TemporaryDir
{
public:
    TemporaryDir()
    {
        std::string pattern = (std::filesystem::temp_directory_path() / "preview-test-XXXXXX").string();
        if (::mkdtemp(pattern.data()))
            m_path = pattern;
    }

    ~TemporaryDir()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryDir(const TemporaryDir&) = delete;
    TemporaryDir& operator=(const TemporaryDir&) = delete;

    std::string filePath(const std::string& name) const { return m_path + "/" + name; }

private:
    std::string m_path;
};

inline std::string writeFile(const std::string& path, const std::string& contents = "text\n")
{
    std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
    return path;
}

// Red on the left half, blue on the right.
inline std::string writeHalvesPng(const std::string& path, int width, int height)
{
    properdesktop::ui::Image image(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixel(x, y, properdesktop::ui::Color::rgb(x < width / 2 ? 0xFF0000 : 0x0000FF));
    image.savePng(path);
    return path;
}

// A grey PNG of this many pixels.
inline std::string writePng(const std::string& path, int width, int height)
{
    properdesktop::ui::Image image(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixel(x, y, properdesktop::ui::Color::rgb(0x808080));
    image.savePng(path);
    return path;
}

inline std::string pdfStream(const std::string& contents)
{
    return "<< /Length " + std::to_string(contents.size()) + " >>\nstream\n" + contents + "\nendstream";
}

// A PDF of these objects, numbered from 1, the first the catalog.
inline std::string pdfOf(const std::vector<std::string>& objects)
{
    std::string out = "%PDF-1.7\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const size_t offset : offsets) {
        char line[21];
        std::snprintf(line, sizeof(line), "%010zu 00000 n \n", offset);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
            + std::to_string(xref) + "\n%%EOF\n";
    return out;
}
