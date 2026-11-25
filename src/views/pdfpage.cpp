// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "pdfpage.h"
#include "cards.h"
#include "passworddialog.h"
#include "pdfview.h"

#include "base/i18n.h"
#include "ui/cell_layout.h"
#include "ui/dialog_window.h"
#include "ui/label.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>
#include <filesystem>

using namespace properdesktop;
using properdesktop::i18n::format;
using properdesktop::i18n::tr;
using ui::rounding::centeredStart;

static constexpr double kCardInset = 12;

PdfPage::PdfPage()
{
    m_view = addChild<PdfView>();
    m_view->setBottomCornerRadius(ui::Theme::current().windowRadius);
    m_find = addChild<FindCard>();
    m_goToPage = addChild<GoToPageCard>();
    m_message = addChild<ui::Label>();
    m_message->setVisible(false);
    m_unlock = addChild<ui::DialogButton>();
    m_unlock->setVisible(false);
    m_unlock->onClicked = [this] { askPassword(); };

    m_find->onEdited = [this] {
        if (!m_document)
            return;
        m_view->setCurrentMatch(-1, -1);
        m_document->setSearch(m_find->text(), m_view->currentPage());
    };
    m_find->onFind = [this](bool backwards) { findNext(backwards); };
    m_find->onClose = [this] { closeCards(); };
    m_goToPage->onAccepted = [this](int page) {
        closeCards();
        m_view->goToPage(page);
    };
    m_goToPage->onClose = [this] { closeCards(); };
    retranslate();
}

PdfPage::~PdfPage()
{
    // Before the document it draws.
    m_view->setDocument(nullptr);
}

void PdfPage::load(const std::string& path)
{
    m_keepsView = path == m_path && isReady() && !m_view->isFitted();
    if (m_keepsView) {
        m_keptZoom = m_view->zoom();
        m_keptPosition = m_view->scrollPosition();
    }
    m_path = path;
    m_view->setDocument(nullptr);
    m_subscriptions.clear();
    m_password.reset();
    if (m_document && onDocumentReplaced)
        onDocumentReplaced();
    m_document = std::make_unique<PdfDocument>(path);
    m_subscriptions.push_back(m_document->stateChanged.connect([this] { stateChanged(); }));
    m_subscriptions.push_back(m_document->searchChanged.connect([this] { searchChanged(); }));
    updateMessage();
}

void PdfPage::clear()
{
    closeCards();
    m_view->setDocument(nullptr);
    m_subscriptions.clear();
    m_password.reset();
    if (m_document && onDocumentReplaced)
        onDocumentReplaced();
    m_document.reset();
    m_path.clear();
    updateMessage();
}

void PdfPage::setTopInset(double inset)
{
    if (inset == m_view->topInset())
        return;
    m_view->setTopInset(inset);
    requestLayout();
}

bool PdfPage::isReady() const
{
    return m_document && m_document->state() == PdfDocument::State::Ready;
}

void PdfPage::retranslate()
{
    m_find->retranslate();
    m_goToPage->retranslate();
    m_unlock->setText(tr("Enter Password…"));
    if (m_password)
        m_password->retranslate();
    updateMessage();
}

void PdfPage::stateChanged()
{
    switch (m_document->state()) {
    case PdfDocument::State::Ready:
        if (m_password)
            m_password->close();
        m_view->setDocument(m_document.get());
        if (m_keepsView) {
            m_keepsView = false;
            m_view->zoomTo(m_keptZoom, m_view->center(), false);
            m_view->setScrollPosition(m_keptPosition);
        }
        if (m_find->isVisible() && !m_find->text().empty())
            m_document->setSearch(m_find->text(), m_view->currentPage());
        break;
    case PdfDocument::State::WrongPassword:
        if (m_password && m_password->isOpen())
            m_password->retry();
        break;
    case PdfDocument::State::Locked:
    case PdfDocument::State::Unreadable:
    case PdfDocument::State::Opening:
        break;
    }
    updateMessage();
    if (onStateChanged)
        onStateChanged();
}

void PdfPage::askPassword()
{
    if (!m_document || !window())
        return;
    if (!m_password || !m_password->isOpen()) {
        m_password = std::make_unique<PasswordDialog>(window(), std::filesystem::path(m_path).filename().string());
        m_password->onAccepted = [this](const std::string& password) { m_document->unlock(password); };
    }
    m_password->show();
}

bool PdfPage::asksPassword() const
{
    return m_password && m_password->isOpen();
}

void PdfPage::updateMessage()
{
    const PdfDocument::State state = m_document ? m_document->state() : PdfDocument::State::Opening;
    const bool locked = state == PdfDocument::State::Locked || state == PdfDocument::State::WrongPassword;
    const bool unreadable = state == PdfDocument::State::Unreadable;
    m_message->setText(locked ? tr("This document is protected by a password.")
                              : tr("This document could not be opened."));
    m_message->setVisible(locked || unreadable);
    m_unlock->setVisible(locked);
    m_view->setVisible(state == PdfDocument::State::Ready);
    if (state != PdfDocument::State::Ready)
        closeCards();
    requestLayout();
}

void PdfPage::showFind()
{
    if (!isReady())
        return;
    m_goToPage->setVisible(false);
    const bool wasShown = m_find->isVisible();
    m_find->open();
    if (!wasShown && !m_find->text().empty())
        m_document->setSearch(m_find->text(), m_view->currentPage());
    requestLayout();
}

void PdfPage::showGoToPage()
{
    if (!isReady())
        return;
    m_find->setVisible(false);
    m_document->setSearch({}, 0);
    m_view->setCurrentMatch(-1, -1);
    m_goToPage->open(m_document->pageCount());
    requestLayout();
}

bool PdfPage::showsCard() const
{
    return m_find->isVisible() || m_goToPage->isVisible();
}

void PdfPage::closeCards()
{
    if (m_find->isVisible() && m_document) {
        m_document->setSearch({}, 0);
        m_view->setCurrentMatch(-1, -1);
    }
    m_find->setVisible(false);
    m_goToPage->setVisible(false);
    requestLayout();
}

// Through the matches found so far, in page order, round at the ends.
void PdfPage::findNext(bool backwards)
{
    if (!isReady())
        return;
    if (!m_find->isVisible()) {
        if (m_document->searchText().empty()) {
            showFind();
            return;
        }
        m_find->setVisible(true);
        requestLayout();
    }
    if (m_document->matchCount() == 0)
        return;
    const int count = m_document->pageCount();
    int page = m_view->matchPage();
    int index = m_view->matchIndex();
    if (page < 0) {
        page = m_view->currentPage();
        index = backwards ? 0 : -1;
    }
    const std::vector<pdf::Match>* matches = m_document->matches(page);
    index += backwards ? -1 : 1;
    if (!matches || index < 0 || index >= int(matches->size())) {
        for (int step = 1; step <= count; ++step) {
            const int next = ((page + (backwards ? -step : step)) % count + count) % count;
            if ((matches = m_document->matches(next))) {
                page = next;
                index = backwards ? int(matches->size()) - 1 : 0;
                break;
            }
        }
    }
    if (matches)
        m_view->setCurrentMatch(page, index);
    searchChanged();
}

void PdfPage::searchChanged()
{
    if (!m_document)
        return;
    const int count = m_document->matchCount();
    // The first match found, from the current page on, until one is picked.
    if (m_view->matchPage() < 0 && count > 0) {
        for (int step = 0; step < m_document->pageCount(); ++step) {
            const int page = (m_view->currentPage() + step) % m_document->pageCount();
            if (m_document->matches(page)) {
                m_view->setCurrentMatch(page, 0);
                break;
            }
        }
    }
    std::string text;
    const bool searched = !m_document->searchText().empty();
    if (searched && count == 0 && m_document->searchFinished()) {
        text = tr("No matches");
    } else if (count > 0 && m_view->matchPage() >= 0) {
        int ordinal = m_view->matchIndex() + 1;
        for (int page = 0; page < m_view->matchPage(); ++page) {
            if (const std::vector<pdf::Match>* matches = m_document->matches(page))
                ordinal += int(matches->size());
        }
        // TRANSLATORS: the current match and the number of matches, such as "2 of 5".
        text = format(tr("{index} of {count}"), {{"index", std::to_string(ordinal)}, {"count", std::to_string(count)}});
    }
    m_find->setCount(text, searched && count == 0 && m_document->searchFinished(), count > 0);
}

void PdfPage::layout()
{
    const ui::RectF rect = localRect();
    m_view->setGeometry(rect);
    const auto place = [&](ui::View* card, double implicitWidth, double height) {
        const double width = std::min(implicitWidth, std::max(0.0, rect.width - kCardInset * 2));
        card->setGeometry({rect.width - kCardInset - width, m_view->topInset() + kCardInset, width, height});
    };
    place(m_find, FindCard::implicitWidth, FindCard::implicitHeight);
    place(m_goToPage, GoToPageCard::implicitWidth, GoToPageCard::implicitHeight);

    const ui::Theme& theme = ui::Theme::current();
    m_message->setColor(theme.text);
    const ui::SizeF message = m_message->sizeHint();
    const ui::SizeF button = m_unlock->sizeHint();
    const double height = message.height + (m_unlock->isVisible() ? 16 + button.height : 0);
    const double inset = m_view->topInset();
    const double top = inset + centeredStart(std::max(0.0, rect.height - inset), height);
    m_message->setGeometry({centeredStart(rect.width, message.width), top, message.width, message.height});
    m_unlock->setGeometry({centeredStart(rect.width, button.width), top + message.height + 16, button.width,
                           button.height});
}
