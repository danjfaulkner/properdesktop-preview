// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "pdfdocument.h"

#include "base/signal.h"
#include "ui/view.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace properdesktop::ui {
class DialogButton;
class Label;
}

class FindCard;
class GoToPageCard;
class PasswordDialog;
class PdfView;

// A PDF under the header: its pages, the find and go to page cards over
// their top right, and the password dialog of a protected file; a file that
// cannot be read, or stays locked, says so instead.
class PdfPage : public properdesktop::ui::View
{
public:
    PdfPage();
    ~PdfPage() override;

    // Opens the file on a worker; onStateChanged tells how it went. Keeps
    // the zoom and the place when it is the file shown, written over.
    void load(const std::string& path);
    void clear();
    void retranslate();

    // Null before load().
    PdfDocument* document() const { return m_document.get(); }
    const std::string& path() const { return m_path; }
    bool isReady() const;
    PdfView* view() const { return m_view; }
    // The view's top inset, under the header; the cards and the message
    // keep below it.
    void setTopInset(double inset);

    // The password dialog, over the window.
    void askPassword();
    bool asksPassword() const;
    PasswordDialog* passwordDialog() const { return m_password.get(); }

    void showFind();
    void findNext(bool backwards);
    void showGoToPage();
    // The find or go to page card is open.
    bool showsCard() const;
    FindCard* findCard() const { return m_find; }
    GoToPageCard* goToPageCard() const { return m_goToPage; }

    // Opened, locked or unreadable.
    std::function<void()> onStateChanged;
    // The document is about to go, for a new one or none.
    std::function<void()> onDocumentReplaced;

protected:
    void layout() override;

private:
    void stateChanged();
    void searchChanged();
    void closeCards();
    void updateMessage();

    std::string m_path;
    std::unique_ptr<PdfDocument> m_document;
    std::vector<properdesktop::base::Subscription> m_subscriptions;
    PdfView* m_view;
    FindCard* m_find;
    GoToPageCard* m_goToPage;
    properdesktop::ui::Label* m_message;
    properdesktop::ui::DialogButton* m_unlock;
    std::unique_ptr<PasswordDialog> m_password;
    // To keep across a reload of the file shown.
    bool m_keepsView = false;
    double m_keptZoom = 0;
    bool m_keptFitted = true;
    properdesktop::ui::PointF m_keptPosition;
};
