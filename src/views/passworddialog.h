// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace properdesktop::ui {
class DialogWindow;
class Window;
}

// The password of a protected PDF. A wrong one is asked for again in the
// same dialog; Cancel leaves the document locked.
class PasswordDialog
{
public:
    PasswordDialog(properdesktop::ui::Window* parent, std::string name);
    ~PasswordDialog();

    void show();
    // The password given was wrong: the field empties and says so.
    void retry();
    void close();
    bool isOpen() const;
    // Null once closed.
    properdesktop::ui::Window* window() const;
    // The password typed stays.
    void retranslate();

    std::function<void(const std::string& password)> onAccepted;

private:
    class Content;

    void build(const std::string& password);

    std::unique_ptr<properdesktop::ui::DialogWindow> m_dialog;
    Content* m_content = nullptr;
    std::string m_name;
    bool m_wrong = false;
};
