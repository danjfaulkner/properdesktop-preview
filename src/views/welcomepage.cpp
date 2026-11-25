// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "welcomepage.h"

#include "base/i18n.h"
#include "ui/cell_layout.h"
#include "ui/color_mixer.h"
#include "ui/color_provider.h"
#include "ui/dialog_window.h"
#include "ui/icon.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace properdesktop;
using properdesktop::i18n::tr;
using ui::rounding::centeredStart;

static constexpr double kIconSize = 72;
static constexpr double kRowHeight = 52;
static constexpr double kButtonHeight = 28;
// From the card's edge to a row's hover plate.
static constexpr double kRowGap = 4;
static constexpr size_t kRecentRows = 5;

static ui::Color textColor(double alpha)
{
    return ui::Theme::current().text.withAlpha(alpha);
}

static double labelHeight(const ui::Label* label)
{
    return std::ceil(label->sizeHint().height);
}

// A view taking a click: pressed, hovered and released inside call onClicked.
class WelcomePage::Clickable : public ui::View
{
public:
    bool acceptsPointer() const override { return true; }
    bool hovered() const { return m_hovered; }

    std::function<void()> onClicked;
    std::function<void()> onHoverChanged;

protected:
    void pointerEnter() override
    {
        m_hovered = true;
        hoverChanged();
    }

    void pointerLeave() override
    {
        m_hovered = false;
        hoverChanged();
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
        if (localRect().contains(event.position) && onClicked)
            onClicked();
    }

    bool m_hovered = false;
    bool m_pressed = false;

private:
    void hoverChanged()
    {
        requestPaint();
        if (onHoverChanged)
            onHoverChanged();
    }
};

// A recent file: its name over its folder.
class WelcomePage::RecentRow : public Clickable
{
public:
    RecentRow(const std::string& path, bool last)
        : m_last(last)
    {
        const std::filesystem::path file(path);
        m_name = addChild<ui::Label>(file.filename().string());
        m_name->setElide(ui::Label::Elide::Middle);
        m_folder = addChild<ui::Label>(file.parent_path().string());
        m_folder->setElide(ui::Label::Elide::Middle);
    }

protected:
    void layout() override
    {
        m_name->setColor(textColor(1));
        m_folder->setColor(textColor(0.5));
        const double left = 16;
        const double width = std::max(0.0, geometry().width - 32);
        const double nameHeight = labelHeight(m_name);
        const double top = centeredStart(geometry().height, nameHeight + 2 + labelHeight(m_folder));
        m_name->setGeometry({left, top, width, m_name->sizeHint().height});
        m_folder->setGeometry({left, top + nameHeight + 2, width, m_folder->sizeHint().height});
    }

    void paint(ui::Painter& painter) override
    {
        const ui::RectF rect = localRect();
        if (m_hovered || m_pressed) {
            const double radius = ui::Theme::current().bigRadius - kRowGap;
            painter.drawRoundedRect(rect.adjusted(kRowGap, kRowGap, -kRowGap, -kRowGap),
                                    {radius, 0, ui::Corners::Circular}, textColor(m_pressed ? 0.12 : 0.06));
        }
        if (!m_last)
            painter.drawRoundedRect({16, rect.height - 1 / painter.scale(), rect.width - 32, 1 / painter.scale()},
                                    {0, 0, ui::Corners::Circular}, textColor(0.12));
    }

private:
    ui::Label* m_name;
    ui::Label* m_folder;
    bool m_last;
};

// The card the recent rows sit in.
class WelcomePage::RecentCard : public ui::View
{
protected:
    void layout() override
    {
        double y = 0;
        for (const auto& child : children()) {
            child->setGeometry({0, y, geometry().width, kRowHeight});
            y += kRowHeight;
        }
    }

    void paint(ui::Painter& painter) override
    {
        const ui::Theme& theme = ui::Theme::current();
        const ui::Corners corners = theme.continuousCorners ? ui::Corners::Continuous : ui::Corners::Circular;
        painter.drawRoundedRect(localRect(), {theme.bigRadius, 0, corners}, theme.secondBackground);
    }
};

// The column under the viewport: as tall as the viewport while it fits,
// centred in it.
class WelcomePage::Column : public ui::View
{
public:
    explicit Column(WelcomePage& page)
        : m_page(page)
    {
        ui::LayoutHints hints;
        hints.fillHeight = true;
        setLayoutHints(hints);
    }

    double heightForWidth(double) const override { return m_page.stackHeight() + 24 * 2; }

protected:
    void layout() override { m_page.layoutColumn(geometry().width, geometry().height); }

private:
    WelcomePage& m_page;
};

WelcomePage::WelcomePage()
{
    setScrollDirections(false, true);
    setBottomCornerRadius(ui::Theme::current().windowRadius);
    setClipsChildren(true);
    m_column = setContent<Column>(*this);
    m_icon = m_column->addChild<ui::ThemeIcon>("properdesktop-preview", kIconSize);
    m_title = m_column->addChild<ui::Label>();
    m_title->setFontSize(15 * 4.0 / 3);
    m_title->setWeight(500);
    m_title->setElide(ui::Label::Elide::None);
    m_subtitle = m_column->addChild<ui::Label>();
    m_subtitle->setElide(ui::Label::Elide::None);
    m_open = m_column->addChild<ui::DialogButton>(std::string(), true);
    m_open->onClicked = [this] {
        if (onOpen)
            onOpen();
    };
    m_recentLabel = m_column->addChild<ui::Label>();
    m_clear = m_column->addChild<Clickable>();
    m_clearLabel = m_clear->addChild<ui::Label>();
    m_clear->onClicked = [this] {
        if (onClearRecent)
            onClearRecent();
    };
    m_clear->onHoverChanged = [this] { m_column->requestLayout(); };
    m_card = m_column->addChild<RecentCard>();
    retranslate();
    setRecentFiles({});
}

void WelcomePage::retranslate()
{
    m_title->setText(tr("Drop an Image or PDF Here"));
    m_subtitle->setText(tr("Or open a file to view it."));
    m_open->setText(tr("Open…"));
    m_recentLabel->setText(tr("Recent"));
    m_clearLabel->setText(tr("Clear"));
    m_column->requestLayout();
}

void WelcomePage::setRecentFiles(const std::vector<std::string>& paths)
{
    for (RecentRow* row : m_rows)
        m_card->takeChild(row);
    m_rows.clear();
    const size_t count = std::min(paths.size(), kRecentRows);
    for (size_t i = 0; i < count; ++i) {
        RecentRow* row = m_card->addChild<RecentRow>(paths[i], i + 1 == count);
        row->onClicked = [this, path = paths[i]] {
            if (onRecentClicked)
                onRecentClicked(path);
        };
        m_rows.push_back(row);
    }
    m_recentLabel->setVisible(count > 0);
    m_clear->setVisible(count > 0);
    m_card->setVisible(count > 0);
    m_column->requestLayout();
}

// The column's own height, without the margin kept above and below it once
// it scrolls.
double WelcomePage::stackHeight() const
{
    double total = kIconSize + 16 + labelHeight(m_title) + 6 + labelHeight(m_subtitle) + 20 + kButtonHeight;
    if (!m_rows.empty())
        total += 32 + std::max(labelHeight(m_recentLabel), labelHeight(m_clearLabel)) + 6
                + kRowHeight * double(m_rows.size());
    return total;
}

void WelcomePage::layoutColumn(double pageWidth, double pageHeight)
{
    m_title->setColor(textColor(1));
    m_subtitle->setColor(textColor(0.65));
    m_recentLabel->setColor(textColor(0.5));
    m_clearLabel->setColor(m_clear->hovered() ? ui::lighter(ui::ColorId::Highlight, 1.2)
                                              : ui::ColorRecipe(ui::ColorId::Highlight));

    const double width = std::min(pageWidth - 48, 440.0);
    const double left = centeredStart(pageWidth, width);
    double y = centeredStart(pageHeight, stackHeight());

    m_icon->setGeometry({left + centeredStart(width, kIconSize), y, kIconSize, kIconSize});
    y += kIconSize + 16;
    const auto centred = [&](ui::Label* label) {
        const double w = std::ceil(label->sizeHint().width);
        label->setGeometry({left + std::round((width - w) / 2), y, w, label->sizeHint().height});
        y += labelHeight(label);
    };
    centred(m_title);
    y += 6;
    centred(m_subtitle);
    y += 20;
    const double openWidth = std::ceil(m_open->sizeHint().width);
    m_open->setGeometry({left + std::round((width - openWidth) / 2), y, openWidth, kButtonHeight});
    y += kButtonHeight;
    if (m_rows.empty())
        return;

    y += 32;
    const double recentRow = std::max(labelHeight(m_recentLabel), labelHeight(m_clearLabel));
    const double clearWidth = std::ceil(m_clearLabel->sizeHint().width);
    const double clearX = left + width - 16 - clearWidth;
    m_recentLabel->setGeometry({left + 16, y + centeredStart(recentRow, labelHeight(m_recentLabel)),
                                std::max(0.0, clearX - 5 - left - 16), m_recentLabel->sizeHint().height});
    m_clear->setGeometry({clearX, y + centeredStart(recentRow, labelHeight(m_clearLabel)), clearWidth,
                          m_clearLabel->sizeHint().height});
    m_clearLabel->setGeometry({0, 0, clearWidth, m_clearLabel->sizeHint().height});
    y += recentRow + 6;
    m_card->setGeometry({left, y, width, kRowHeight * double(m_rows.size())});
}
