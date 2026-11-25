// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "passworddialog.h"

#include "base/i18n.h"
#include "ui/cell_layout.h"
#include "ui/dialog_window.h"
#include "ui/keys.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/text_field.h"
#include "ui/window.h"

using namespace properdesktop;
using properdesktop::i18n::format;
using properdesktop::i18n::tr;

class PasswordDialog::Content : public ui::ColumnLayout
{
public:
    explicit Content(PasswordDialog* dialog)
        : m_dialog(dialog)
    {
        setSpacing(12);
        // The dialog pads the sides and the bottom; the top takes 16 more.
        setPadding({0, 16, 0, 0});

        auto* message = addChild<ui::Label>(
                dialog->m_wrong ? format(tr("The password for “{name}” is incorrect. Try again."), {{"name", dialog->m_name}})
                                : format(tr("“{name}” is protected. Enter its password to open it."),
                                         {{"name", dialog->m_name}}));
        message->setWrap(true);
        message->setColor(ui::ColorId::DisabledText);
        message->setLayoutHints({.fillWidth = true});

        password = addChild<ui::TextField>();
        password->setName("passwordField");
        password->setPlaceholder(tr("Password"));
        password->setEchoMode(ui::TextField::EchoMode::Password);
        password->setLayoutHints({.fillWidth = true});
        password->onAccepted = [this] { accept(); };
        password->onTextChanged = [this] { open->setEnabled(!password->text().empty()); };

        auto* actions = addChild<ui::RowLayout>();
        actions->setSpacing(8);
        ui::LayoutHints actionHints {.horizontalAlignment = ui::Alignment::End};
        actionHints.margins.top = 4;
        actions->setLayoutHints(actionHints);
        auto* cancel = actions->addChild<ui::DialogButton>(tr("Cancel"));
        cancel->onClicked = [this] { m_dialog->m_dialog->close(); };
        open = actions->addChild<ui::DialogButton>(tr("Open"), true);
        open->onClicked = [this] { accept(); };
        open->setEnabled(false);
    }

    void accept()
    {
        if (!password->text().empty() && m_dialog->onAccepted)
            m_dialog->onAccepted(password->text());
    }

    ui::TextField* password;
    ui::DialogButton* open;

private:
    PasswordDialog* m_dialog;
};

PasswordDialog::PasswordDialog(ui::Window* parent, std::string name)
    : m_name(std::move(name))
{
    m_dialog = std::make_unique<ui::DialogWindow>(parent, tr("Password Required"), 400);
    // Cancel closes it.
    m_dialog->setCloseVisible(false);
    m_dialog->window()->setBackgroundColor(ui::ColorId::SecondBackground);
    m_dialog->window()->addShortcut(ui::NoModifier, ui::key::Escape, [this] { m_dialog->close(); });
    build({});
}

PasswordDialog::~PasswordDialog() = default;

void PasswordDialog::build(const std::string& password)
{
    m_content = m_dialog->setContent<Content>(this);
    m_content->password->setText(password);
    m_content->open->setEnabled(!password.empty());
    m_content->password->setFocus();
}

void PasswordDialog::show()
{
    m_dialog->show();
}

void PasswordDialog::retry()
{
    m_wrong = true;
    build({});
}

void PasswordDialog::close()
{
    m_dialog->close();
}

bool PasswordDialog::isOpen() const
{
    return m_dialog->window() != nullptr;
}

ui::Window* PasswordDialog::window() const
{
    return m_dialog->window();
}

void PasswordDialog::retranslate()
{
    // Closed: it goes once the event is over.
    if (!m_dialog->window())
        return;
    m_dialog->setTitle(tr("Password Required"));
    build(m_content->password->text());
}
