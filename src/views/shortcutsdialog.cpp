// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "shortcutsdialog.h"

#include "base/i18n.h"
#include "ui/cell_layout.h"
#include "ui/color_provider.h"
#include "ui/dialog_window.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/painter.h"
#include "ui/scroll_view.h"
#include "ui/shortcut_convention.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace properdesktop;
using properdesktop::i18n::tr;
using properdesktop::i18n::trNoop;
using ui::rounding::centeredStart;

static constexpr double kRowHeight = 44;

static ui::Label* addLabel(ui::View* parent, std::string text, std::optional<ui::ColorRecipe> color = std::nullopt)
{
    auto* label = parent->addChild<ui::Label>(std::move(text));
    if (color)
        label->setColor(color);
    label->setElide(ui::Label::Elide::Right);
    return label;
}

// A rounded card of rows 44 tall, a separator under each but the last.
class ShortcutsDialog::Card : public ui::View
{
public:
    double heightForWidth(double) const override { return kRowHeight * double(children().size()); }
    ui::SizeF sizeHint() const override { return {0, heightForWidth(0)}; }

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
        for (size_t i = 0; i + 1 < children().size(); ++i) {
            const ui::RectF row = children()[i]->geometry();
            painter.drawRoundedRect({16, row.y + row.height - 1 / painter.scale(), row.width - 32, 1 / painter.scale()},
                                    {0, 0, ui::Corners::Circular}, ui::color(ui::ColorId::Separator));
        }
    }
};

// What a key does, and the key at the end.
class ShortcutsDialog::Row : public ui::View
{
public:
    Row(std::string text, std::string keys)
        : m_text(addLabel(this, std::move(text)))
        , m_keys(addLabel(this, std::move(keys), ui::ColorId::DisabledText))
    {
        m_keys->setTextAlignment(ui::Label::TextAlignment::Right);
    }

protected:
    void layout() override
    {
        const double width = geometry().width;
        const double height = geometry().height;
        const ui::SizeF keys = m_keys->sizeHint();
        // Long keys would otherwise squeeze the text out.
        const double keysWidth = std::min(std::ceil(keys.width), std::round(width * 0.65));
        m_keys->setGeometry({width - 16 - keysWidth, centeredStart(height, keys.height), keysWidth, keys.height});
        const ui::SizeF text = m_text->sizeHint();
        m_text->setGeometry({16, centeredStart(height, text.height), std::max(0.0, width - 16 - keysWidth - 12 - 16),
                             text.height});
    }

private:
    ui::Label* m_text;
    ui::Label* m_keys;
};

// The sections' column: margins 16 at the sides, 4 above, 20 below; titles
// 16 further in, 8 above their cards, sections 12 apart.
class ShortcutsDialog::Column : public ui::View
{
public:
    void addSection(std::string title, Card* card)
    {
        m_sections.push_back({addLabel(this, std::move(title), ui::ColorId::DisabledText), card});
    }

    double heightForWidth(double width) const override
    {
        double height = 4;
        for (const auto& [title, card] : m_sections)
            height += std::ceil(title->sizeHint().height) + 8 + card->heightForWidth(width - 32) + 12;
        return height - 12 + 20;
    }

protected:
    void layout() override
    {
        const double width = std::max(0.0, geometry().width - 32);
        double y = 4;
        for (const auto& [title, card] : m_sections) {
            const double titleHeight = std::ceil(title->sizeHint().height);
            title->setGeometry({32, y, std::max(0.0, width - 32), titleHeight});
            y += titleHeight + 8;
            const double height = card->heightForWidth(width);
            card->setGeometry({16, y, width, height});
            y += height + 12;
        }
    }

private:
    std::vector<std::pair<ui::Label*, Card*>> m_sections;
};

// 600 tall in all; the list scrolls inside.
class ShortcutsDialog::Scroller : public ui::View
{
public:
    Scroller()
        : scroll(addChild<ui::ScrollView>())
    {
        scroll->setScrollDirections(false, true);
        scroll->setBottomCornerRadius(ui::Theme::current().windowRadius);
    }

    double heightForWidth(double) const override { return 600 - 44; }

    ui::ScrollView* scroll;

protected:
    void layout() override
    {
        scroll->setGeometry(localRect());
    }
};

ShortcutsDialog::ShortcutsDialog(ui::Window* parent)
    : m_parent(parent)
{
}

ShortcutsDialog::~ShortcutsDialog() = default;

void ShortcutsDialog::show()
{
    if (m_dialog && m_dialog->window()) {
        m_dialog->window()->activate();
        return;
    }
    struct Section
    {
        const char* title;
        std::vector<std::pair<const char*, const char*>> keys;
    };
    const char* trash = ui::commandKeys() ? "Ctrl+Backspace" : "Delete";
    const std::vector<Section> sections = {
        {trNoop("Images"), {
            {trNoop("Previous or next image"), "← →"},
            {trNoop("Previous or next image"), "Page Up, Page Down"},
            {trNoop("Previous or next image"), "Shift+Space, Space"},
            {trNoop("First or last image"), "Home, End"},
            {trNoop("Rotate left or right"), "Ctrl+L, Ctrl+R"},
            {trNoop("Copy"), "Ctrl+C"},
            {trNoop("Move to Trash"), trash},
        }},
        {trNoop("PDF Documents"), {
            {trNoop("Previous or next page"), "← →"},
            {trNoop("A screenful up or down"), "Page Up, Page Down"},
            {trNoop("A screenful up or down"), "Shift+Space, Space"},
            {trNoop("First or last page"), "Home, End"},
            {trNoop("Go to page"), "Ctrl+Alt+G"},
            {trNoop("Find"), "Ctrl+F"},
            {trNoop("Next or previous match"), "Ctrl+G, Ctrl+Shift+G"},
            {trNoop("Select all"), "Ctrl+A"},
            {trNoop("Copy"), "Ctrl+C"},
        }},
        {trNoop("Zoom"), {
            {trNoop("Zoom in or out"), "Ctrl+= Ctrl+-"},
            {trNoop("Zoom to fit"), "Ctrl+0"},
            {trNoop("Actual size"), "Ctrl+9"},
        }},
        {trNoop("Window"), {
            {trNoop("Full screen"), "F, F11"},
            {trNoop("Leave full screen"), "Esc"},
            {trNoop("Open a file"), "Ctrl+O"},
            {trNoop("Close"), "Ctrl+W"},
            {trNoop("Keyboard shortcuts"), "Ctrl+?"},
            {trNoop("Quit"), "Ctrl+Q"},
        }},
    };

    m_dialog = std::make_unique<ui::DialogWindow>(m_parent, tr("Keyboard Shortcuts"), 480);
    m_dialog->window()->setBackgroundColor(ui::ColorId::Background);
    ui::DialogWindow* dialog = m_dialog.get();
    m_dialog->window()->addShortcut(ui::NoModifier, ui::key::Escape, [dialog] { dialog->close(); });
    m_dialog->setEdgeToEdge(true);
    auto* scroller = m_dialog->setContent<Scroller>();
    auto* column = scroller->scroll->setContent<Column>();
    for (const Section& section : sections) {
        auto* card = column->addChild<Card>();
        for (const auto& [text, keys] : section.keys)
            card->addChild<Row>(tr(text), keys);
        column->addSection(tr(section.title), card);
    }
    m_dialog->show();
}
