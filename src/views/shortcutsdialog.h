// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <memory>

namespace properdesktop::ui {
class DialogWindow;
class Window;
}

// Help > Keyboard Shortcuts: the keys, in cards by what they do, as the
// Video Player lists its own.
class ShortcutsDialog
{
public:
    explicit ShortcutsDialog(properdesktop::ui::Window* parent);
    ~ShortcutsDialog();

    void show();

private:
    class Card;
    class Column;
    class Row;
    class Scroller;

    properdesktop::ui::Window* m_parent;
    std::unique_ptr<properdesktop::ui::DialogWindow> m_dialog;
};
