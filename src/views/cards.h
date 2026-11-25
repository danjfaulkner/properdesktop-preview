// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "ui/text_field.h"
#include "ui/view.h"

#include <functional>
#include <optional>
#include <string>

namespace properdesktop::ui {
class Icon;
class Label;
}

// A round plate inset a tenth of its size, tinted with the text colour when
// hovered or pressed, around a glyph: the Text Editor's card buttons.
class RoundButton : public properdesktop::ui::View
{
public:
    explicit RoundButton(double size = 28);

    void setGlyph(std::string name, double iconSize = 16);

    bool acceptsPointer() const override { return true; }

    std::function<void()> onClicked;

protected:
    void layout() override;
    void paint(properdesktop::ui::Painter& painter) override;
    void enabledChanged() override;
    void pointerEnter() override;
    void pointerLeave() override;
    void pointerPress(const properdesktop::ui::PointerEvent& event) override;
    void pointerRelease(const properdesktop::ui::PointerEvent& event) override;

private:
    double m_size;
    double m_iconSize = 16;
    properdesktop::ui::Icon* m_icon = nullptr;
    bool m_hovered = false;
    bool m_pressed = false;
};

// A text field that hands Return, Shift+Return and Escape to its owner.
class CardField : public properdesktop::ui::TextField
{
public:
    std::function<void(bool shift)> onReturn;
    std::function<void()> onEscape;

protected:
    bool keyPress(const properdesktop::ui::KeyEvent& event) override;
};

// The menus' surface colour, rounded, a hairline edge and a soft shadow, as
// the Text Editor's floating cards; its content inset by `padding`.
class FloatingCard : public properdesktop::ui::View
{
public:
    static constexpr double padding = 6;

    FloatingCard();

    properdesktop::ui::RectF visualRect() const override;
    bool acceptsPointer() const override { return true; }

protected:
    void paint(properdesktop::ui::Painter& painter) override;
};

// The Text Editor's find card without its options: the field with the
// count inside it, previous, next and close. Its owner searches.
class FindCard : public FloatingCard
{
public:
    static constexpr double implicitWidth = 360;
    static constexpr double implicitHeight = 32 + padding * 2;

    FindCard();

    void open();
    std::string text() const { return m_field->text(); }
    // Beside the text, such as "2 of 5"; red when nothing matches.
    void setCount(const std::string& count, bool notFound, bool matches);
    std::string countText() const;
    void retranslate();

    std::function<void()> onEdited;
    std::function<void(bool backwards)> onFind;
    std::function<void()> onClose;

protected:
    void layout() override;

private:
    CardField* m_field;
    properdesktop::ui::Label* m_count;
    RoundButton* m_previous;
    RoundButton* m_next;
    RoundButton* m_close;
};

// "Page" in a small card where the find card floats; Enter goes there,
// Escape closes it.
class GoToPageCard : public FloatingCard
{
public:
    static constexpr double implicitWidth = 300;
    static constexpr double implicitHeight = 32 + padding * 2;

    GoToPageCard();

    void open(int pageCount);
    void retranslate();

    // From 0.
    std::function<void(int page)> onAccepted;
    std::function<void()> onClose;

protected:
    void layout() override;

private:
    // From 0; -1 when the text is no page.
    int target() const;
    void refresh();

    CardField* m_field;
    RoundButton* m_close;
    int m_pageCount = 0;
};
