// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cards.h"

#include "base/i18n.h"
#include "ui/cell_layout.h"
#include "ui/color_provider.h"
#include "ui/icon.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/theme.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace properdesktop::ui;
using properdesktop::i18n::format;
using properdesktop::i18n::tr;

static constexpr double kButtonSize = 28;
static constexpr double kFieldHeight = 32;
static constexpr double kSpacing = 2;

RoundButton::RoundButton(double size)
    : m_size(size)
{
}

void RoundButton::setGlyph(std::string name, double iconSize)
{
    if (!m_icon)
        m_icon = addChild<Icon>(std::move(name), iconSize);
    else
        m_icon->setIconName(std::move(name));
    m_icon->setSize(iconSize);
    m_iconSize = iconSize;
    requestLayout();
}

void RoundButton::layout()
{
    if (!m_icon)
        return;
    m_icon->setGeometry({std::round((geometry().width - m_iconSize) / 2), std::round((geometry().height - m_iconSize) / 2),
                         m_iconSize, m_iconSize});
    m_icon->setColor(Theme::current().text);
    m_icon->setOpacity(isEnabled() ? 1 : 0.35);
}

void RoundButton::paint(Painter& painter)
{
    if (!isEnabled() || (!m_pressed && !m_hovered))
        return;
    const Color fill = color(m_pressed ? ColorId::RoundButtonPressed : ColorId::RoundButtonHovered);
    // Whole device pixels of the icon's parity, so the snapped plate and the
    // snapped icon share a centre wherever the button lands.
    const double ratio = painter.scale();
    const auto plateLength = [&](double length) {
        double pixels = std::round(length * ratio);
        if (int64_t(pixels - std::round(m_iconSize * ratio)) % 2 != 0)
            pixels -= 1;
        return pixels / ratio;
    };
    const double margin = std::round(m_size * 0.1);
    const double width = plateLength(geometry().width - margin * 2);
    const double height = plateLength(geometry().height - margin * 2);
    const double iconX = std::round((geometry().width - m_iconSize) / 2);
    const double iconY = std::round((geometry().height - m_iconSize) / 2);
    const RectF plate {iconX + (m_iconSize - width) / 2, iconY + (m_iconSize - height) / 2, width, height};
    painter.drawRoundedRect(plate, {std::min(width, height) / 2, 0, Corners::Circular}, fill);
}

void RoundButton::enabledChanged()
{
    requestLayout();
    requestPaint();
}

void RoundButton::pointerEnter()
{
    m_hovered = true;
    requestPaint();
}

void RoundButton::pointerLeave()
{
    m_hovered = false;
    requestPaint();
}

void RoundButton::pointerPress(const PointerEvent& event)
{
    if (event.button != MouseButton::Left)
        return;
    m_pressed = true;
    requestPaint();
}

void RoundButton::pointerRelease(const PointerEvent& event)
{
    if (!m_pressed)
        return;
    m_pressed = false;
    requestPaint();
    if (localRect().contains(event.position) && onClicked)
        onClicked();
}

bool CardField::keyPress(const KeyEvent& event)
{
    if ((event.key == key::Return || event.key == key::KeypadEnter) && onReturn) {
        onReturn(event.modifiers & ShiftModifier);
        return true;
    }
    if (event.key == key::Escape && onEscape) {
        onEscape();
        return true;
    }
    return TextField::keyPress(event);
}

// A shadow radius of 16 is a Gaussian of deviation (16 + 1) / 3.3333.
static constexpr double kShadowSigma = 17 / 3.3333;

FloatingCard::FloatingCard()
{
    setVisible(false);
}

RectF FloatingCard::visualRect() const
{
    const double reach = Painter::shadowReach(kShadowSigma);
    return localRect().adjusted(-reach, -reach + 3, reach, reach + 3);
}

void FloatingCard::paint(Painter& painter)
{
    const Theme& theme = Theme::current();
    // Concentric with the field inside; too small a card for continuous corners.
    const double radius = theme.smallRadius + padding;
    const RectF plate = localRect();
    painter.drawShadow(plate, radius, kShadowSigma, Color {0, 0, 0, 255}.withAlpha(theme.darkMode ? 0.35 : 0.16),
                       {0, 3});
    painter.drawRoundedRect(plate, {radius, 0, Corners::Circular}, theme.secondBackground, 1 / painter.scale(),
                            theme.text.withAlpha(theme.darkMode ? 0.12 : 0.08));
}

FindCard::FindCard()
    : m_field(addChild<CardField>())
    , m_count(addChild<Label>())
    , m_previous(addChild<RoundButton>(kButtonSize))
    , m_next(addChild<RoundButton>(kButtonSize))
    , m_close(addChild<RoundButton>(kButtonSize))
{
    m_field->onTextEdited = [this] {
        if (onEdited)
            onEdited();
    };
    m_field->onReturn = [this](bool shift) {
        if (onFind)
            onFind(shift);
    };
    m_field->onEscape = [this] {
        if (onClose)
            onClose();
    };
    m_count->setFontSize(12);
    m_count->setVisible(false);
    m_previous->setGlyph("chevron-up");
    m_previous->onClicked = [this] {
        if (onFind)
            onFind(true);
    };
    m_next->setGlyph("chevron-down");
    m_next->onClicked = [this] {
        if (onFind)
            onFind(false);
    };
    m_close->setGlyph("close");
    m_close->onClicked = [this] {
        if (onClose)
            onClose();
    };
    retranslate();
    setCount({}, false, false);
}

void FindCard::retranslate()
{
    m_field->setPlaceholder(tr("Find"));
}

void FindCard::open()
{
    setVisible(true);
    m_field->setFocus();
    m_field->selectAll();
}

void FindCard::setCount(const std::string& count, bool notFound, bool matches)
{
    m_count->setText(count);
    m_count->setVisible(!count.empty());
    m_count->setColor(notFound ? ColorRecipe(accent::red) : ColorRecipe(ColorId::DisabledText));
    m_field->setTextColor(notFound ? std::optional<ColorRecipe>(accent::red) : std::nullopt);
    m_previous->setEnabled(matches);
    m_next->setEnabled(matches);
    requestLayout();
}

std::string FindCard::countText() const
{
    return m_count->isVisible() ? m_count->text() : std::string();
}

void FindCard::layout()
{
    const double width = geometry().width - padding * 2;
    const double buttonY = padding + rounding::centeredStart(kFieldHeight, kButtonSize);
    const double fieldWidth = std::max(0.0, width - (kButtonSize + kSpacing) * 3);
    m_field->setGeometry({padding, padding, fieldWidth, kFieldHeight});
    double x = padding + fieldWidth + kSpacing;
    for (RoundButton* button : {m_previous, m_next, m_close}) {
        button->setGeometry({x, buttonY, kButtonSize, kButtonSize});
        x += kButtonSize + kSpacing;
    }
    // The count inside the field, 4 from its end.
    double inside = 0;
    if (m_count->isVisible()) {
        const SizeF size = m_count->sizeHint();
        inside = size.width + 4;
        m_count->setGeometry({padding + fieldWidth - inside, padding + rounding::centeredStart(kFieldHeight, size.height),
                              size.width, size.height});
    }
    m_field->setTextMargins(Theme::current().largeSpacing, inside + 8);
}

GoToPageCard::GoToPageCard()
    : m_field(addChild<CardField>())
    , m_close(addChild<RoundButton>(kButtonSize))
{
    m_field->setValidator([](const std::string& text) {
        return std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || c == ' '; });
    });
    m_field->onTextEdited = [this] { refresh(); };
    m_field->onReturn = [this](bool) {
        const int page = target();
        if (page >= 0 && onAccepted)
            onAccepted(page);
    };
    m_field->onEscape = [this] {
        if (onClose)
            onClose();
    };
    m_close->setGlyph("close");
    m_close->onClicked = [this] {
        if (onClose)
            onClose();
    };
}

void GoToPageCard::open(int pageCount)
{
    m_pageCount = pageCount;
    setVisible(true);
    m_field->setText({});
    refresh();
    m_field->setFocus();
}

void GoToPageCard::retranslate()
{
    refresh();
}

int GoToPageCard::target() const
{
    std::string digits;
    for (const char c : m_field->text()) {
        if (c != ' ')
            digits += c;
    }
    if (digits.empty() || digits.size() > 9)
        return -1;
    const int page = std::stoi(digits);
    return page >= 1 && page <= m_pageCount ? page - 1 : -1;
}

void GoToPageCard::refresh()
{
    // TRANSLATORS: {count} is the number of pages of the document.
    m_field->setPlaceholder(format(tr("Go to page (1\xE2\x80\x93{count})"), {{"count", std::to_string(m_pageCount)}}));
    m_field->setTextColor(!m_field->text().empty() && target() < 0 ? std::optional<ColorRecipe>(accent::red)
                                                                   : std::nullopt);
}

void GoToPageCard::layout()
{
    const double width = geometry().width - padding * 2;
    m_field->setGeometry({padding, padding, std::max(0.0, width - kSpacing - kButtonSize), kFieldHeight});
    m_close->setGeometry({padding + width - kButtonSize, padding + rounding::centeredStart(kFieldHeight, kButtonSize),
                          kButtonSize, kButtonSize});
}
