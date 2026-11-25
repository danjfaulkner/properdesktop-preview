// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "imagesequence.h"

#include "base/collator.h"
#include "base/mime_database.h"
#include "ui/image_info.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

using namespace properdesktop;
namespace fs = std::filesystem;

// The folder's images and `own`, sorted as the folder's videos are queued.
// Image types go by the name's extension, looked up once per extension: the
// MIME database's glob match takes tens of microseconds a name.
static std::vector<std::string> folderImages(const fs::path& folder, const std::string& own)
{
    base::Collator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitive(false);
    std::map<std::string, bool> extensions;
    std::vector<std::pair<std::string, std::string>> names;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError))
            continue;
        const std::string name = entry.path().filename().string();
        if (name != own) {
            if (name.starts_with('.'))
                continue;
            const std::string extension = entry.path().extension().string();
            auto known = extensions.find(extension);
            if (known == extensions.end())
                known = extensions.emplace(extension, ImageSequence::isImage(name)).first;
            if (!known->second)
                continue;
        }
        names.emplace_back(collator.sortKey(name), name);
    }
    std::sort(names.begin(), names.end());

    std::vector<std::string> paths;
    paths.reserve(names.size());
    for (const auto& [key, name] : names)
        paths.push_back((folder / name).string());
    return paths;
}

static bool exists(const std::string& path)
{
    std::error_code error;
    return fs::is_regular_file(path, error);
}

ImageSequence::ImageSequence(std::vector<std::string> paths, int index, bool followsFolder)
    : m_paths(std::move(paths))
    , m_index(index)
    , m_followsFolder(followsFolder)
{
    // Copying a file in alone brings its creation, writes, attributes and
    // rename one by one.
    m_settle.setSingleShot(true);
    m_settle.setInterval(std::chrono::milliseconds(100));
    m_settle.onTimeout = [this] { applyChanges(); };
    watch();
}

std::unique_ptr<ImageSequence> ImageSequence::folderOf(const std::string& path)
{
    auto sequence = std::unique_ptr<ImageSequence>(
            new ImageSequence({fs::path(path).lexically_normal().string()}, 0, true));
    sequence->listFolder();
    return sequence;
}

std::unique_ptr<ImageSequence> ImageSequence::files(std::vector<std::string> paths)
{
    return std::unique_ptr<ImageSequence>(new ImageSequence(std::move(paths), 0, false));
}

bool ImageSequence::isImage(const std::string& path)
{
    const std::vector<std::string>& types = ui::imageMimeTypes();
    return std::find(types.begin(), types.end(), base::mime::typeForFileName(fs::path(path).filename().string()))
            != types.end();
}

std::string ImageSequence::currentPath() const
{
    return m_paths.empty() ? std::string() : m_paths[size_t(m_index)];
}

void ImageSequence::setCurrentIndex(int index)
{
    index = std::clamp(index, 0, std::max(0, count() - 1));
    if (index == m_index)
        return;
    m_index = index;
    changed.emit();
}

void ImageSequence::remove(const std::string& path)
{
    std::vector<std::string> paths = m_paths;
    std::erase(paths, path);
    replace(std::move(paths));
}

void ImageSequence::replace(std::vector<std::string> paths)
{
    if (paths == m_paths)
        return;
    const std::string current = currentPath();
    const auto found = std::find(paths.begin(), paths.end(), current);
    if (found != paths.end())
        m_index = int(found - paths.begin());
    else
        m_index = std::clamp(m_index, 0, std::max(0, int(paths.size()) - 1));
    m_paths = std::move(paths);
    changed.emit();
}

void ImageSequence::watch()
{
    std::set<std::string> folders;
    for (const std::string& path : m_paths)
        folders.insert(fs::path(path).parent_path().string());
    for (const std::string& folder : folders) {
        m_watches.push_back(base::FileWatch::directory(
                folder, [this, folder](const std::string& name) { folderChanged(folder, name); }));
    }
}

void ImageSequence::folderChanged(const std::string& folder, const std::string& name)
{
    if (!name.empty() && (fs::path(folder) / name).string() == currentPath())
        m_currentTouched = true;
    m_settle.start();
}

void ImageSequence::applyChanges()
{
    const std::string current = currentPath();
    if (m_followsFolder) {
        listFolder();
    } else {
        std::vector<std::string> paths;
        for (const std::string& path : m_paths) {
            if (exists(path))
                paths.push_back(path);
        }
        replace(std::move(paths));
    }
    const bool written = m_currentTouched && exists(current);
    m_currentTouched = false;
    if (written)
        currentWritten.emit();
}

void ImageSequence::listFolder()
{
    const std::string current = currentPath();
    const fs::path folder = fs::path(current).parent_path();
    const std::string own = exists(current) ? fs::path(current).filename().string() : std::string();
    m_listing = base::ThreadPool::global().run([folder, own] { return folderImages(folder, own); },
                                               [this](std::vector<std::string> paths) { replace(std::move(paths)); });
}
