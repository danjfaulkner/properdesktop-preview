// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "previewsettings.h"

#include "base/paths.h"
#include "base/settings.h"

#include <algorithm>
#include <filesystem>

using namespace properdesktop;

static constexpr const char* kRecentKey = "Recent/Files";
static constexpr const char* kSidebarWidthKey = "Sidebar/Width";

static std::string sidebarKey(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Image:
        return "Sidebar/Images";
    case DocumentKind::Pdf:
        return "Sidebar/Documents";
    }
    return {};
}

PreviewSettings::PreviewSettings(std::string file)
    : m_file(std::move(file))
{
    const base::Settings settings(m_file);
    m_recentFiles = settings.stringListValue(kRecentKey);
    if (m_recentFiles.size() > kRecentCapacity)
        m_recentFiles.resize(kRecentCapacity);
}

std::string PreviewSettings::defaultPath()
{
    return base::paths::properdesktopConfig("preview");
}

bool PreviewSettings::sidebarShown(DocumentKind kind, bool unset) const
{
    return base::Settings(m_file).boolValue(sidebarKey(kind), unset);
}

void PreviewSettings::setSidebarShown(DocumentKind kind, bool shown)
{
    base::Settings settings(m_file);
    settings.setValue(sidebarKey(kind), shown);
    settings.sync();
}

double PreviewSettings::sidebarWidth(double unset) const
{
    return base::Settings(m_file).doubleValue(kSidebarWidthKey, unset);
}

void PreviewSettings::setSidebarWidth(double width)
{
    base::Settings settings(m_file);
    settings.setValue(kSidebarWidthKey, width);
    settings.sync();
}

std::vector<std::string> PreviewSettings::recentFiles() const
{
    std::vector<std::string> files;
    std::error_code error;
    for (const std::string& path : m_recentFiles) {
        if (std::filesystem::exists(path, error))
            files.push_back(path);
    }
    return files;
}

void PreviewSettings::addRecentFile(const std::string& path)
{
    if (!m_recentFiles.empty() && m_recentFiles.front() == path)
        return;
    std::erase(m_recentFiles, path);
    m_recentFiles.insert(m_recentFiles.begin(), path);
    if (m_recentFiles.size() > kRecentCapacity)
        m_recentFiles.resize(kRecentCapacity);
    writeRecentFiles();
    recentFilesChanged.emit();
}

void PreviewSettings::clearRecentFiles()
{
    m_recentFiles.clear();
    writeRecentFiles();
    recentFilesChanged.emit();
}

void PreviewSettings::writeRecentFiles()
{
    base::Settings settings(m_file);
    if (m_recentFiles.empty())
        settings.remove(kRecentKey);
    else
        settings.setValue(kRecentKey, m_recentFiles);
    settings.sync();
}
