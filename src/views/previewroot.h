// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "previewwindow.h"

#include "ui/file_drop_area.h"
#include "ui/view.h"

#include <functional>

namespace properdesktop::ui {
class HeaderBackdrop;
class HeaderButton;
class HeaderButtonGroup;
class Label;
class SidebarDivider;
class WindowHeader;
}

class WelcomePage;

// The header. In full screen it has a strip of its own, the window's colour
// with a hairline along its bottom, to stay readable over the image; else
// the content's header backdrop lies under it.
class PreviewWindow::HeaderBar : public properdesktop::ui::View
{
public:
    explicit HeaderBar(double height);

    properdesktop::ui::WindowHeader* header;
    // Where the content starts, past the sidebar.
    double contentLeft = 0;
    bool strip = false;

protected:
    void layout() override;
    void paint(properdesktop::ui::Painter& painter) override;
};

// The header's own part: the sidebar toggle after the window buttons, the
// arrows with the place in the sequence and the file name from the
// sidebar's edge, the zoom buttons at the end. A PDF has no arrows: its file
// name, then the page in the secondary colour on the same line.
class PreviewWindow::HeaderRow : public properdesktop::ui::View
{
public:
    explicit HeaderRow(PreviewWindow& owner, properdesktop::ui::WindowHeader* header);

    // Each plate shows while one of its buttons does.
    void updateGroups();

    properdesktop::ui::HeaderButtonGroup* sidebarGroup;
    properdesktop::ui::HeaderButton* sidebar;
    properdesktop::ui::HeaderButtonGroup* arrows;
    properdesktop::ui::HeaderButton* previous;
    properdesktop::ui::HeaderButton* next;
    properdesktop::ui::Label* position;
    properdesktop::ui::Label* title;
    // "Page 5 of 120", shown while showsPage is set and it fits.
    properdesktop::ui::Label* page;
    bool showsPage = false;
    properdesktop::ui::HeaderButtonGroup* tools;
    properdesktop::ui::HeaderButton* zoomOut;
    properdesktop::ui::HeaderButton* zoomIn;
    properdesktop::ui::HeaderButton* rotate;
    properdesktop::ui::HeaderButton* search;
    double sidebarWidth = 0;
    // Off in full screen, where the window buttons hide.
    bool windowButtons = true;

protected:
    void layout() override;

private:
    static properdesktop::ui::HeaderButton* addButton(properdesktop::ui::HeaderButtonGroup* group, const char* glyph,
                                                 std::function<void()> action, double glyphSize = 16);
    static void place(properdesktop::ui::HeaderButtonGroup* group, double x, double y);

    properdesktop::ui::WindowHeader* m_header;
};

// The window's content: the sidebar down the left edge, the page beside it
// under the header, and in full screen the page under the header too, on
// black. Files dropped on it open as File > Open opens them.
class PreviewWindow::Root : public properdesktop::ui::FileDropArea
{
public:
    explicit Root(PreviewWindow& owner);

    // As wide as asked, within what leaves the content its width at the
    // window's minimum size.
    double sidebarWidth() const;
    // Where the content starts: as much of the sidebar as has slid in.
    double contentLeft() const;

    Sidebar* sidebar;
    ImagePage* image;
    PdfPage* pdf;
    WelcomePage* welcome;
    properdesktop::ui::HeaderBackdrop* imageBackdrop;
    properdesktop::ui::HeaderBackdrop* pdfBackdrop;
    HeaderBar* bar;
    HeaderRow* row;
    properdesktop::ui::SidebarDivider* divider;

protected:
    void layout() override;
    void paint(properdesktop::ui::Painter& painter) override;
    void hoverMove(const properdesktop::ui::PointerEvent& event) override;

private:
    PreviewWindow& m_owner;
    properdesktop::ui::PointF m_lastHover {-1, -1};
};
