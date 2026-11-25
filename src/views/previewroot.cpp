// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "previewroot.h"
#include "application.h"
#include "imagepage.h"
#include "pdfpage.h"
#include "pdfview.h"
#include "sidebar.h"
#include "welcomepage.h"

#include "base/url.h"
#include "ui/cell_layout.h"
#include "ui/color_provider.h"
#include "ui/header_backdrop.h"
#include "ui/header_button.h"
#include "ui/header_button_group.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/sidebar_divider.h"
#include "ui/theme.h"
#include "ui/window.h"
#include "ui/window_controls.h"
#include "ui/window_header.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>


using namespace properdesktop;
using ui::rounding::centeredStart;

// The header's spacing: the window buttons' inset from the window's edge,
// used at both ends and past the sidebar's edge; one gap between neighbouring
// groups, text included; a smaller one between the texts of one run.
static constexpr double kHeaderInset = 9;
static constexpr double kGroupGap = 12;
static constexpr double kTextGap = 8;
// What the content keeps beside a shown sidebar at the window's minimum size,
// as in Files: the sidebar grows only as the window does.
static constexpr double kMinimumContentWidthBesideSidebar =
        PreviewWindow::kMinimumContentWidth - Sidebar::kDefaultWidth;

PreviewWindow::HeaderBar::HeaderBar(double height)
{
    header = addChild<ui::WindowHeader>(height);
    header->setButtonMargin(4);
    header->setButtonsCentered(true);
    header->setContentUnderButtons(true);
}

void PreviewWindow::HeaderBar::layout()
{
    header->setGeometry(localRect());
}

void PreviewWindow::HeaderBar::paint(ui::Painter& painter)
{
    if (!strip)
        return;
    const ui::RectF rect = localRect();
    painter.fillRect({contentLeft, 0, rect.width - contentLeft, rect.height}, ui::Theme::current().background);
    const double line = 1 / painter.scale();
    painter.drawRoundedRect({contentLeft, rect.height - line, rect.width - contentLeft, line}, {0},
                            ui::color(ui::ColorId::HeaderSeparator));
}

PreviewWindow::HeaderRow::HeaderRow(PreviewWindow& owner, ui::WindowHeader* header)
    : m_header(header)
{
    sidebarGroup = addChild<ui::HeaderButtonGroup>();
    // The toolkit's sidebar glyph is drawn on 24 pixels with a one-pixel line.
    sidebar = addButton(sidebarGroup, "sidebar", [&owner] { owner.toggleSidebar(); }, 24);
    arrows = addChild<ui::HeaderButtonGroup>();
    previous = addButton(arrows, "chevron-left", [&owner] { owner.step(-1); });
    next = addButton(arrows, "chevron-right", [&owner] { owner.step(1); });
    position = addChild<ui::Label>();
    title = addChild<ui::Label>();
    title->setWeight(700);
    title->setElide(ui::Label::Elide::Middle);
    page = addChild<ui::Label>();
    page->setVisible(false);
    tools = addChild<ui::HeaderButtonGroup>();
    zoomOut = addButton(tools, "zoom-out", [&owner] { owner.zoomOut(); });
    zoomIn = addButton(tools, "zoom-in", [&owner] { owner.zoomIn(); });
    rotate = addButton(tools, "rotate-left", [&owner] { owner.rotate(-1); });
    search = addButton(tools, "search", [&owner] { owner.showFind(); });
}

void PreviewWindow::HeaderRow::updateGroups()
{
    for (ui::HeaderButtonGroup* group : {sidebarGroup, arrows, tools})
        group->setVisible(group->sizeHint().width > 0);
}

void PreviewWindow::HeaderRow::layout()
{
    const double height = geometry().height;
    const double scale = window() ? window()->scale() : 1;
    // On whole device pixels, so equal gaps come out equal.
    const auto snap = [scale](double value) { return std::round(value * scale) / scale; };
    // A plate is drawn on whole device pixels, its width rounded down.
    const auto plateWidth = [scale](const ui::HeaderButtonGroup* plate) {
        return std::floor(plate->sizeHint().width * scale) / scale;
    };
    const double top = centeredStart(height, ui::HeaderButtonGroup::kSegment);
    // The capitals centred on the buttons, as Settings' title is.
    const auto textTop = [height, scale](const ui::Label* label) {
        return std::round((height / 2 - label->capCenter()) * scale) / scale;
    };
    double x = kHeaderInset;
    if (windowButtons) {
        x = m_header->leadingInset() - ui::window_controls::kSpacing + kGroupGap;
    }
    place(sidebarGroup, snap(x), top);
    x = sidebarGroup->geometry().x + plateWidth(sidebarGroup) + kGroupGap;
    // From the sidebar's edge as the Files header's buttons are.
    x = snap(std::max(x, sidebarWidth + kHeaderInset));
    if (arrows->isVisible()) {
        place(arrows, x, top);
        x = snap(x + plateWidth(arrows) + kGroupGap);
    }
    if (position->isVisible()) {
        const ui::SizeF hint = position->sizeHint();
        position->setColor(ui::ColorId::Text);
        const double width = std::ceil(hint.width);
        position->setGeometry({x, textTop(position), width, hint.height});
        x = snap(x + width + kTextGap);
    }
    // The window shows only whole device pixels of a fractional width.
    double right = (std::floor(geometry().width * scale) - std::round(kHeaderInset * scale)) / scale;
    // Rotate for an image, search for a PDF, in one place.
    if (tools->isVisible()) {
        right = snap(right - plateWidth(tools));
        place(tools, right, top);
        right -= kGroupGap;
    }
    const ui::SizeF hint = title->sizeHint();
    title->setColor(ui::ColorId::Text);
    if (showsPage) {
        // In line with the page's left edge at fit.
        if (sidebarWidth > 0)
            x = std::max(x, snap(sidebarWidth + PdfView::kMargin));
        // The page text stays whole while it fits; the name takes the rest.
        const double available = std::max(0.0, right - x);
        const ui::SizeF pageHint = page->sizeHint();
        const double pageWidth = std::ceil(pageHint.width);
        page->setVisible(pageWidth <= available);
        const double nameWidth = page->isVisible() ? std::max(0.0, available - kTextGap - pageWidth)
                                                   : available;
        const double y = textTop(title);
        title->setGeometry({x, y, std::min(nameWidth, std::ceil(hint.width)), hint.height});
        if (page->isVisible()) {
            const std::optional<ui::Label::LineGeometry> line = title->lineGeometry(0);
            const double nameEnd = x + std::ceil(line ? line->width : 0);
            page->setColor(ui::ColorId::DisabledText);
            page->setGeometry({nameEnd + kTextGap, y, pageWidth, pageHint.height});
        }
        return;
    }
    page->setVisible(false);
    title->setGeometry({x, textTop(title), std::max(0.0, right - x), hint.height});
}

ui::HeaderButton* PreviewWindow::HeaderRow::addButton(ui::HeaderButtonGroup* group, const char* glyph,
                                                      std::function<void()> action, double glyphSize)
{
    ui::HeaderButton* button = group->addButton(glyph, glyphSize);
    button->onClicked = std::move(action);
    return button;
}

void PreviewWindow::HeaderRow::place(ui::HeaderButtonGroup* group, double x, double y)
{
    const ui::SizeF size = group->sizeHint();
    group->setGeometry({x, y, size.width, size.height});
}

PreviewWindow::Root::Root(PreviewWindow& owner)
    : m_owner(owner)
{
    setTracksHover(true);
    sidebar = addChild<Sidebar>(kHeaderHeight);
    image = addChild<ImagePage>();
    pdf = addChild<PdfPage>();
    welcome = addChild<WelcomePage>();
    // Over the content, under the header.
    imageBackdrop = addChild<ui::HeaderBackdrop>(image->viewer());
    pdfBackdrop = addChild<ui::HeaderBackdrop>(pdf->view());
    bar = addChild<HeaderBar>(kHeaderHeight);
    row = bar->header->setContent<HeaderRow>(owner, bar->header);
    divider = addChild<ui::SidebarDivider>(Sidebar::kMinimumWidth, Sidebar::kMaximumWidth, Sidebar::kDefaultWidth);
    divider->setName("sidebarDivider");
    divider->onResized = [this](double width) {
        m_owner.m_sidebarWidth = width;
        sidebar->setResizing(true);
        requestLayout();
    };
    divider->onFinished = [this](double width) {
        m_owner.m_sidebarWidth = width;
        sidebar->setResizing(false);
        m_owner.m_application.settings().setSidebarWidth(width);
        requestLayout();
    };
    filter = [](const std::string& url) { return base::url::isLocalFile(url); };
    onDropped = [this](const std::vector<std::string>& urls) {
        std::vector<std::string> paths;
        for (const std::string& url : urls)
            paths.push_back(base::url::toLocalFile(url).value_or(std::string()));
        m_owner.m_application.open(paths);
    };
}

double PreviewWindow::Root::sidebarWidth() const
{
    const double widest = std::max(Sidebar::kMinimumWidth,
                                   std::min(Sidebar::kMaximumWidth,
                                            geometry().width - kMinimumContentWidthBesideSidebar));
    return std::min(m_owner.m_sidebarWidth, widest);
}

double PreviewWindow::Root::contentLeft() const
{
    return sidebar->isVisible() ? sidebarWidth() * m_owner.m_sidebarReveal : 0;
}

void PreviewWindow::Root::layout()
{
    const ui::RectF rect = localRect();
    sidebar->setVisible(m_owner.m_sidebarReveal > 0 && !m_owner.isWelcome());
    const bool pdfShown = !m_owner.isWelcome() && m_owner.kind() == DocumentKind::Pdf;
    image->setVisible(!m_owner.isWelcome() && !pdfShown);
    pdf->setVisible(pdfShown);
    welcome->setVisible(m_owner.isWelcome());
    const double sidebarWidth = this->sidebarWidth();
    const double left = contentLeft();
    const bool fullScreen = m_owner.isFullScreen();
    // The content runs under the header and rests below it.
    const double inset = fullScreen ? 0 : kHeaderHeight;
    sidebar->setGeometry({left - sidebarWidth, 0, sidebarWidth, rect.height});
    divider->setVisible(sidebar->isVisible() && m_owner.m_sidebarReveal == 1);
    divider->setSidebarWidth(left);
    divider->setGeometry({left - ui::SidebarDivider::kWidth / 2, 0, ui::SidebarDivider::kWidth, rect.height});
    const double width = std::max(0.0, rect.width - left);
    const ui::RectF content {left, 0, width, rect.height};
    image->setTopInset(inset);
    image->setGeometry(content);
    pdf->setTopInset(inset);
    pdf->view()->setPageShadows(!fullScreen);
    pdf->setGeometry(content);
    welcome->setGeometry({left, kHeaderHeight, width, std::max(0.0, rect.height - kHeaderHeight)});
    imageBackdrop->setVisible(!fullScreen && image->isVisible());
    pdfBackdrop->setVisible(!fullScreen && pdfShown);
    imageBackdrop->setGeometry({left, 0, width, kHeaderHeight});
    pdfBackdrop->setGeometry({left, 0, width, kHeaderHeight});
    bar->strip = fullScreen;
    bar->requestPaint();
    bar->contentLeft = left;
    bar->setGeometry({0, 0, rect.width, kHeaderHeight});
    row->sidebarWidth = left;
    row->windowButtons = !fullScreen;
    row->requestLayout();
    if (window())
        window()->setBackgroundBlur(rect, ui::Theme::current().windowRadius);
}

void PreviewWindow::Root::paint(ui::Painter& painter)
{
    const double left = contentLeft();
    const ui::RectF rect = localRect();
    painter.fillRect({left, 0, rect.width - left, rect.height},
                     m_owner.isFullScreen() ? ui::Color {0, 0, 0, 255} : ui::Theme::current().background);
}

void PreviewWindow::Root::hoverMove(const ui::PointerEvent& event)
{
    // Surface changes resend motion at the same spot; that is not activity.
    const ui::PointF position = mapToWindow(event.position);
    if (position.x == m_lastHover.x && position.y == m_lastHover.y)
        return;
    m_lastHover = position;
    m_owner.poke();
}
