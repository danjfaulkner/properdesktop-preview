// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "sidebar.h"
#include "pdfdocument.h"

#include "ui/cell_layout.h"
#include "ui/color_provider.h"
#include "ui/icon.h"
#include "ui/image_view.h"
#include "ui/label.h"
#include "ui/list_view.h"
#include "ui/painter.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>
#include <cmath>

using namespace properdesktop;
using ui::rounding::centeredStart;

static constexpr double kSmallSpacing = 6;
// A file row at the default width; rows grow with the sidebar.
static constexpr double kRowHeight = 120;
static constexpr double kRowSpacing = 3;
// From the selection's edge to the thumbnail.
static constexpr double kThumbnailGap = 8;
static constexpr double kNumberGap = 4;
// A page's thumbnail inside its plate: this far from its sides and top, and
// from the number to the plate's bottom.
static constexpr double kPageGap = 6;
static constexpr double kPageMaximumAspect = 1.5;
static constexpr double kNumberFontSize = 11;
static constexpr double kOutlineRowHeight = 28;
static constexpr double kOutlineIndent = 14;
static constexpr double kChevronSize = 12;

// The Files sidebar's: one and a half small spacings, on whole device pixels.
static double plateInset(const ui::View* view)
{
    const double scale = view->window() ? view->window()->scale() : 1;
    return std::round(kSmallSpacing * 1.5 * scale) / scale;
}

// The selected thumbnail's plate is the accent, pressed or not.
static void paintPlate(ui::Painter& painter, const ui::View* view, bool pressed, bool selected, bool accent,
                       ui::Corners corners = ui::Corners::Circular)
{
    ui::Color fill {0, 0, 0, 0};
    if (selected && accent)
        fill = ui::Theme::current().highlight;
    else if (pressed)
        fill = ui::color(ui::ColorId::ItemPressed);
    else if (selected)
        fill = ui::Theme::current().text.withAlpha(0.05);
    if (fill.a == 0)
        return;
    const double inset = plateInset(view);
    painter.drawRoundedRect({inset, 0, view->geometry().width - inset * 2, view->geometry().height},
                            {ui::Theme::current().mediumRadius, 0, corners}, fill);
}

// One thumbnail on the selection's rounded plate: a file's, or a page's
// with its number under it.
class Sidebar::Row : public ui::View
{
public:
    explicit Row(Sidebar& sidebar)
        : m_sidebar(sidebar)
    {
        m_thumbnail = addChild<ui::ImageView>();
        m_thumbnail->setThumbnail(true);
        m_thumbnail->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
        m_number = addChild<ui::Label>();
        m_number->setFontSize(kNumberFontSize);
        m_number->setTextAlignment(ui::Label::TextAlignment::Center);
    }

    void bind(size_t row)
    {
        m_row = int(row);
        const bool page = m_sidebar.m_document;
        m_thumbnail->setVisible(!page);
        m_number->setVisible(page);
        m_thumbnail->setSource(page ? std::string() : m_sidebar.m_paths[row]);
        m_number->setText(page ? std::to_string(row + 1) : std::string());
        requestLayout();
        requestPaint();
    }

    int row() const { return m_row; }
    bool acceptsPointer() const override { return true; }

protected:
    void layout() override
    {
        const double inset = plateInset(this);
        const ui::RectF area = thumbnailArea();
        m_thumbnail->setGeometry(area);
        const ui::SizeF number = m_number->sizeHint();
        m_number->setColor(m_row == m_sidebar.m_current ? ui::ColorRecipe(ui::ColorId::HighlightedText)
                                                        : ui::ColorRecipe(ui::ColorId::DisabledText));
        m_number->setGeometry({inset, area.bottom() + kNumberGap, std::max(0.0, geometry().width - 2 * inset),
                               std::ceil(number.height)});
        if (m_sidebar.m_document)
            requestThumbnail();
    }

    void paint(ui::Painter& painter) override
    {
        PdfDocument* document = m_sidebar.m_document;
        const ui::Theme& theme = ui::Theme::current();
        // A page row is a card: continuous corners where the theme has them;
        // a file row keeps the controls' circular ones.
        const ui::Corners corners = document && theme.continuousCorners ? ui::Corners::Continuous
                                                                         : ui::Corners::Circular;
        paintPlate(painter, this, m_pressed, m_row == m_sidebar.m_current, true, corners);
        if (!document || m_row < 0 || m_row >= document->pageCount())
            return;
        const ui::RectF rect = pageRect(painter.scale());
        const double line = 1 / painter.scale();
        // Concentric with the plate, a page gap inside it.
        const double radius = theme.mediumRadius - kPageGap;
        painter.drawRoundedRect(rect.adjusted(-line, -line, line, line), {radius + line, 0, corners},
                                theme.text.withAlpha(theme.darkMode ? 0.2 : 0.15));
        painter.drawRoundedRect(rect, {radius, 0, corners}, {255, 255, 255, 255});
        if (const ui::RasterImage* image = document->anyThumbnail(m_row)) {
            painter.save();
            painter.clipRoundedRect(rect, radius, corners);
            painter.drawImage(image->skImage(), rect);
            painter.restore();
        }
    }

    void pointerPress(const ui::PointerEvent& event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        m_pressed = true;
        requestPaint();
    }

    void pointerRelease(const ui::PointerEvent& event) override
    {
        if (!m_pressed)
            return;
        m_pressed = false;
        requestPaint();
        if (localRect().contains(event.position) && m_sidebar.onActivated)
            m_sidebar.onActivated(m_row);
    }

public:
    // A file's thumbnail fits in it; a page's is fitted and centred in it.
    ui::RectF thumbnailArea() const
    {
        if (m_sidebar.m_document)
            return {plateInset(this) + kPageGap, kPageGap, m_sidebar.m_pageWidth, m_sidebar.m_pageHeight};
        const double inset = plateInset(this) + kThumbnailGap;
        return {inset, kThumbnailGap, std::max(0.0, geometry().width - 2 * inset),
                std::max(0.0, geometry().height - 2 * kThumbnailGap)};
    }

    // The page's thumbnail, on whole device pixels and as many of them as
    // its render has.
    ui::RectF pageRect(double ratio) const
    {
        const base::Size pixels = pagePixels(ratio);
        const ui::RectF area = thumbnailArea();
        const double width = pixels.width() / ratio;
        const double height = pixels.height() / ratio;
        return {std::round((area.x + (area.width - width) / 2) * ratio) / ratio,
                std::round((area.y + (area.height - height) / 2) * ratio) / ratio, width, height};
    }

    ui::Label* number() const { return m_number; }

private:
    // The page fitted into the area's device pixels: the full width, or the
    // full height for a page too tall for it; asked for as the render's box,
    // so the render has exactly these pixels.
    base::Size pagePixels(double ratio) const
    {
        const ui::SizeF page = m_sidebar.m_document->pageSizes()[size_t(m_row)];
        const ui::RectF area = thumbnailArea();
        const int width = int(std::round(area.width * ratio));
        const int height = int(std::round(area.height * ratio));
        const int fitted = std::max(1, int(std::round(page.height * width / page.width)));
        if (fitted <= height)
            return {width, fitted};
        return {std::max(1, int(std::round(page.width * height / page.height))), height};
    }

    void requestThumbnail()
    {
        if (m_sidebar.m_resizing)
            return;
        const double ratio = window() && window()->scale() > 0 ? window()->scale() : 1;
        m_sidebar.m_document->thumbnail(m_row, pagePixels(ratio));
    }

    Sidebar& m_sidebar;
    ui::ImageView* m_thumbnail;
    ui::Label* m_number;
    int m_row = -1;
    bool m_pressed = false;
};

// An item of the outline, indented by its depth, with a chevron that opens
// and closes the items under it.
class Sidebar::OutlineRow : public ui::View
{
public:
    explicit OutlineRow(Sidebar& sidebar)
        : m_sidebar(sidebar)
    {
        m_chevron = addChild<ui::Icon>("chevron-right-small", kChevronSize);
        m_title = addChild<ui::Label>();
        m_title->setElide(ui::Label::Elide::Right);
    }

    void bind(size_t row)
    {
        m_row = row;
        const OutlineEntry& entry = m_sidebar.m_outlineRows[row];
        m_chevron->setVisible(!entry.item->children.empty());
        m_chevron->setIconName(m_sidebar.m_open.contains(entry.item) ? "chevron-down-small" : "chevron-right-small");
        m_title->setText(entry.item->title);
        requestLayout();
    }

    bool acceptsPointer() const override { return true; }

protected:
    void layout() override
    {
        const ui::Theme& theme = ui::Theme::current();
        const double height = geometry().height;
        const double x = plateInset(this) + kThumbnailGap + m_sidebar.m_outlineRows[m_row].depth * kOutlineIndent;
        m_chevron->setGeometry({x, centeredStart(height, kChevronSize), kChevronSize, kChevronSize});
        m_chevron->setColor(theme.text);
        const double left = x + kChevronSize + 4;
        const ui::SizeF title = m_title->sizeHint();
        m_title->setColor(theme.text);
        m_title->setGeometry({left, centeredStart(height, title.height),
                              std::max(0.0, geometry().width - plateInset(this) - kThumbnailGap - left), title.height});
    }

    void paint(ui::Painter& painter) override { paintPlate(painter, this, m_pressed, false, false); }

    void pointerPress(const ui::PointerEvent& event) override
    {
        if (event.button != ui::MouseButton::Left)
            return;
        m_pressed = true;
        requestPaint();
    }

    void pointerRelease(const ui::PointerEvent& event) override
    {
        if (!m_pressed)
            return;
        m_pressed = false;
        requestPaint();
        if (!localRect().contains(event.position))
            return;
        if (m_chevron->isVisible() && event.position.x < m_title->geometry().x)
            m_sidebar.toggleContentsRow(m_row);
        else
            m_sidebar.activateContentsRow(m_row);
    }

private:
    Sidebar& m_sidebar;
    ui::Icon* m_chevron;
    ui::Label* m_title;
    size_t m_row = 0;
    bool m_pressed = false;
};

Sidebar::Sidebar(double headerHeight)
    : m_headerHeight(headerHeight)
{
    m_list = addChild<ui::ListView>();
    m_list->setRowHeight(kRowHeight);
    m_list->setSpacing(kRowSpacing);
    // Space above the first plate that scrolls away with the rows.
    m_list->setMargins({0, kSmallSpacing, 0, 0});
    m_list->createItem = [this] { return std::make_unique<Row>(*this); };
    m_list->bindItem = [](ui::View* item, size_t row) { static_cast<Row*>(item)->bind(row); };
    m_subscriptions.push_back(m_list->scrolled.connect([this] { keepVisibleThumbnails(); }));
    m_outline = addChild<ui::ListView>();
    m_outline->setVisible(false);
    m_outline->setRowHeight(kOutlineRowHeight);
    m_outline->setMargins({0, kSmallSpacing, 0, 0});
    m_outline->createItem = [this] { return std::make_unique<OutlineRow>(*this); };
    m_outline->bindItem = [](ui::View* item, size_t row) { static_cast<OutlineRow*>(item)->bind(row); };
}

Sidebar::~Sidebar() = default;

void Sidebar::setItems(const std::vector<std::string>& paths)
{
    if (paths == m_paths && !m_document)
        return;
    setDocument(nullptr);
    m_paths = paths;
    m_list->setRowCount(m_paths.size());
    m_list->reload();
}

void Sidebar::setDocument(PdfDocument* document)
{
    if (document == m_document)
        return;
    m_subscriptions.resize(1);
    m_document = document;
    m_paths.clear();
    m_open.clear();
    m_outlineRows.clear();
    m_showsContents = false;
    if (document) {
        m_subscriptions.push_back(document->thumbnailChanged.connect([this](int page) {
            if (ui::View* row = m_list->itemForRow(size_t(page)))
                row->requestPaint();
        }));
        m_subscriptions.push_back(document->outlineChanged.connect([this] { outlineChanged(); }));
    }
    // One height for every row: that of the page tallest for its width at
    // the thumbnail's width, so pages of other shapes centre in the same box.
    m_pageAspect = 0;
    if (document) {
        for (const ui::SizeF& page : document->pageSizes())
            m_pageAspect = std::max(m_pageAspect, page.height / page.width);
    }
    updateRowHeight();
    m_list->setRowCount(document ? size_t(document->pageCount()) : 0);
    m_list->reload();
    outlineChanged();
}

size_t Sidebar::count() const
{
    return m_list->rowCount();
}

void Sidebar::setCurrent(int index)
{
    // The number's colour follows, set as the row lays out.
    if (m_current >= 0) {
        if (ui::View* row = m_list->itemForRow(size_t(m_current))) {
            row->requestLayout();
            row->requestPaint();
        }
    }
    m_current = index;
    if (index < 0)
        return;
    if (ui::View* row = m_list->itemForRow(size_t(index))) {
        row->requestLayout();
        row->requestPaint();
    }
    m_list->scrollToRow(size_t(index));
}

void Sidebar::refresh()
{
    m_list->reload();
}

bool Sidebar::hasContents() const
{
    return m_document && !m_document->outline().empty();
}

void Sidebar::setShowsContents(bool contents)
{
    contents = contents && hasContents();
    m_showsContents = contents;
    m_list->setVisible(!contents);
    m_outline->setVisible(contents);
    requestLayout();
}

void Sidebar::outlineChanged()
{
    m_open.clear();
    flattenOutline();
    setShowsContents(m_showsContents);
}

void Sidebar::flattenOutline()
{
    m_outlineRows.clear();
    if (m_document) {
        const auto add = [this](const auto& self, const std::vector<pdf::OutlineItem>& items, int depth) -> void {
            for (const pdf::OutlineItem& item : items) {
                m_outlineRows.push_back({&item, depth});
                if (m_open.contains(&item))
                    self(self, item.children, depth + 1);
            }
        };
        add(add, m_document->outline(), 0);
    }
    m_outline->setRowCount(m_outlineRows.size());
    m_outline->reload();
}

const std::string& Sidebar::contentsTitle(size_t row) const
{
    return m_outlineRows[row].item->title;
}

void Sidebar::toggleContentsRow(size_t row)
{
    const pdf::OutlineItem* item = m_outlineRows[row].item;
    if (!m_open.erase(item))
        m_open.insert(item);
    flattenOutline();
}

void Sidebar::activateContentsRow(size_t row)
{
    if (onDestinationActivated)
        onDestinationActivated(m_outlineRows[row].item->destination);
}

void Sidebar::keepVisibleThumbnails()
{
    if (!m_document)
        return;
    const double step = m_list->rowHeight() + kRowSpacing;
    const double top = std::max(0.0, m_list->scrollPosition().y - kSmallSpacing);
    const int first = int(top / step);
    const int last = int((top + m_list->geometry().height) / step);
    m_document->keepThumbnailRequests(first - 1, last + 1);
}

ui::RectF Sidebar::rowGeometry(int page) const
{
    const ui::View* row = m_list->itemForRow(size_t(page));
    if (!row)
        return {};
    const ui::PointF origin = row->mapToWindow({});
    return {origin.x, origin.y, row->geometry().width, row->geometry().height};
}

ui::RectF Sidebar::pageThumbnailGeometry(int page, double ratio) const
{
    const auto* row = static_cast<const Row*>(m_list->itemForRow(size_t(page)));
    if (!row || !m_document)
        return {};
    const ui::RectF rect = row->pageRect(ratio);
    const ui::PointF origin = row->mapToWindow(rect.topLeft());
    return {origin.x, origin.y, rect.width, rect.height};
}

ui::RectF Sidebar::pageNumberGeometry(int page) const
{
    const auto* row = static_cast<const Row*>(m_list->itemForRow(size_t(page)));
    if (!row)
        return {};
    const ui::View* number = row->number();
    const ui::PointF origin = number->mapToWindow({});
    return {origin.x, origin.y, number->geometry().width, number->geometry().height};
}

void Sidebar::updateRowHeight()
{
    const double ratio = window() && window()->scale() > 0 ? window()->scale() : 1;
    const double width = geometry().width > 0 ? geometry().width : kDefaultWidth;
    double height = 0;
    if (!m_document) {
        height = std::round(kRowHeight * width / kDefaultWidth * ratio) / ratio;
    } else {
        // On whole device pixels, so the tallest page fills its box exactly.
        m_pageWidth = std::max(1.0, width - 2 * (plateInset(this) + kPageGap));
        m_pageHeight = std::round(std::min(m_pageAspect, kPageMaximumAspect) * m_pageWidth * ratio) / ratio;
        height = kPageGap + m_pageHeight + kNumberGap + numberHeight(ratio) + kPageGap;
    }
    m_list->setRowHeight(height);
}

// Measured again only for another scale or font.
double Sidebar::numberHeight(double ratio)
{
    const ui::Theme& theme = ui::Theme::current();
    if (m_numberHeight <= 0 || ratio != m_numberRatio || theme.fontFamily != m_numberFont) {
        ui::Label number("0");
        number.setFontSize(kNumberFontSize);
        m_numberHeight = std::ceil(number.sizeHint().height);
        m_numberRatio = ratio;
        m_numberFont = theme.fontFamily;
    }
    return m_numberHeight;
}

void Sidebar::setResizing(bool resizing)
{
    if (resizing == m_resizing)
        return;
    m_resizing = resizing;
    if (!resizing)
        m_list->reload();
}

void Sidebar::layout()
{
    updateRowHeight();
    const double top = m_headerHeight + kSmallSpacing;
    const ui::RectF rest {0, top, geometry().width, std::max(0.0, geometry().height - top)};
    m_list->setGeometry(rest);
    m_outline->setGeometry(rest);
}

void Sidebar::paint(ui::Painter& painter)
{
    // As the Files sidebar: a step below the window's colour in both modes,
    // and the header backdrop's hairline down the edge, 0.5 px, below 2x a
    // device pixel with its alpha scaled to the same weight.
    painter.fillRect(localRect(), ui::Theme::current().background.withAlpha(0.7));
    const double scale = painter.scale();
    const double devicePixels = std::max(1.0, std::round(0.5 * scale));
    const double lineWidth = devicePixels / scale;
    const ui::Color separator = ui::color(ui::ColorId::HeaderSeparator);
    painter.drawRoundedRect({geometry().width - lineWidth, 0, lineWidth, geometry().height}, {0},
                            separator.withAlpha(separator.a / 255.0 * 0.5 * scale / devicePixels));
}
