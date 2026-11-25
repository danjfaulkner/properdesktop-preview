// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/signal.h"

#include <string>
#include <vector>

// What a window shows; the sidebar is kept per kind.
enum class DocumentKind {
    Image,
    Pdf,
};

// preview.conf: whether the sidebar shows, per kind, and the files opened
// recently.
class PreviewSettings
{
public:
    static constexpr size_t kRecentCapacity = 20;

    explicit PreviewSettings(std::string file);

    // ~/.config/properdesktop/preview.conf.
    static std::string defaultPath();

    // `unset` until the sidebar is toggled for the kind.
    bool sidebarShown(DocumentKind kind, bool unset = false) const;
    void setSidebarShown(DocumentKind kind, bool shown);
    // One for every kind; `unset` until the sidebar is resized.
    double sidebarWidth(double unset) const;
    void setSidebarWidth(double width);

    // Local paths, newest first; files gone from disk are left out.
    std::vector<std::string> recentFiles() const;
    void addRecentFile(const std::string& path);
    void clearRecentFiles();

    properdesktop::base::Signal<> recentFilesChanged;

private:
    void writeRecentFiles();

    std::string m_file;
    std::vector<std::string> m_recentFiles;
};
