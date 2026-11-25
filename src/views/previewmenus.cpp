// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "previewwindow.h"
#include "application.h"
#include "imagepage.h"
#include "pdfpage.h"
#include "pdfview.h"
#include "previewroot.h"
#include "sidebar.h"

#include "base/i18n.h"
#include "ui/keys.h"
#include "ui/menu.h"
#include "ui/shortcut_convention.h"
#include "ui/window.h"

#include <filesystem>


using namespace properdesktop;
using properdesktop::i18n::tr;
using properdesktop::i18n::trc;

void PreviewWindow::buildMainMenu()
{
    // Starts reading the shortcut convention, which the trash key follows.
    ui::commandKeys();
    m_window->setMainMenu(nullptr);
    m_mainMenu = std::make_unique<ui::Menu>();
    const uint32_t ctrl = ui::ControlModifier;
    const uint32_t shift = ui::ShiftModifier;
    const uint32_t alt = ui::AltModifier;
    const auto image = [this] { return !isWelcome() && m_kind == DocumentKind::Image && m_root->image->showsImage(); };
    const auto pdf = [this] { return !isWelcome() && m_kind == DocumentKind::Pdf && m_root->pdf->isReady(); };

    ui::Menu* application = m_mainMenu->addSubmenu(tr("Preview"));
    application->setRole(ui::MenuRole::Application);
    application->addItem(tr("About Preview"), [this] { showAbout(); });
    application->addSeparator();
    application->addItem(tr("Hide Preview"), [this] { m_window->hideApplication(); });
    application->addSeparator();
    const size_t quit = application->addItem(tr("Quit Preview"), [this] { m_application.quit(); });
    application->setItemKeyEquivalent(quit, "q", ctrl);

    ui::Menu* file = m_mainMenu->addSubmenu(tr("File"));
    const size_t open = file->addItem(tr("Open…"), [this] { openDialog(); });
    file->setItemKeyEquivalent(open, "o", ctrl);
    const size_t recentRow = open + 1;
    ui::Menu* recent = file->addSubmenu(tr("Open Recent"));
    recent->onAboutToShow = [this, recent] {
        recent->clear();
        for (const std::string& path : m_application.settings().recentFiles()) {
            recent->addItem(std::filesystem::path(path).filename().string(),
                            [this, path] { m_application.open({path}); });
        }
        recent->addSeparator();
        recent->addItem(tr("Clear Recent Files"), [this] { m_application.settings().clearRecentFiles(); });
    };
    file->addSeparator();
    const size_t closeWindow = file->addItem(tr("Close Window"), [this] { close(); });
    file->setItemKeyEquivalent(closeWindow, "w", ctrl);
    file->addSeparator();
    const size_t show = file->addItem(tr("Show in Files"), [this] { showInFiles(); });
    const size_t trash = file->addItem(tr("Move to Trash"), [this] { moveToTrash(); });
    file->onAboutToShow = [this, file, recentRow, show, trash] {
        file->setItemEnabled(recentRow, !m_application.settings().recentFiles().empty());
        file->setItemEnabled(show, !isWelcome());
        file->setItemEnabled(trash, !isWelcome());
        // The trash key differs between the two shortcut conventions, as in Files.
        if (ui::commandKeys())
            file->setItemKeyEquivalent(trash, "BackSpace", ui::ControlModifier);
        else
            file->setItemKeyEquivalent(trash, "Delete", 0);
    };

    ui::Menu* edit = m_mainMenu->addSubmenu(tr("Edit"));
    const size_t copyRow = edit->addItem(tr("Copy"), [this] { copy(); });
    edit->setItemKeyEquivalent(copyRow, "c", ctrl);
    const size_t selectAll = edit->addItem(tr("Select All"), [this] { this->selectAll(); });
    edit->setItemKeyEquivalent(selectAll, "a", ctrl);
    edit->addSeparator();
    const size_t find = edit->addItem(tr("Find…"), [this] { showFind(); });
    edit->setItemKeyEquivalent(find, "f", ctrl);
    const size_t findNext = edit->addItem(tr("Find Next"), [this] { this->findNext(false); });
    edit->setItemKeyEquivalent(findNext, "g", ctrl);
    const size_t findPrevious = edit->addItem(tr("Find Previous"), [this] { this->findNext(true); });
    edit->setItemKeyEquivalent(findPrevious, "g", ctrl | shift);
    edit->onAboutToShow = [this, edit, image, pdf, copyRow, selectAll, find, findNext, findPrevious] {
        edit->setItemEnabled(copyRow, image() || (pdf() && m_root->pdf->view()->hasSelection()));
        // Text and search are a PDF's.
        for (const size_t row : {selectAll, find, findNext, findPrevious})
            edit->setItemEnabled(row, pdf());
    };

    ui::Menu* view = m_mainMenu->addSubmenu(tr("View"));
    const size_t sidebarRow = view->addItem(tr("Show Sidebar"), [this] { toggleSidebar(); });
    const size_t thumbnails = view->addItem(tr("Thumbnails"), [this] { setShowsContents(false); });
    const size_t contents = view->addItem(tr("Contents"), [this] { setShowsContents(true); });
    view->addSeparator();
    const size_t zoomInRow = view->addItem(tr("Zoom In"), [this] { zoomIn(); });
    view->setItemKeyEquivalent(zoomInRow, "=", ctrl);
    const size_t zoomOutRow = view->addItem(tr("Zoom Out"), [this] { zoomOut(); });
    view->setItemKeyEquivalent(zoomOutRow, "-", ctrl);
    const size_t fit = view->addItem(tr("Zoom to Fit"), [this] { zoomToFit(); });
    view->setItemKeyEquivalent(fit, "0", ctrl);
    const size_t actual = view->addItem(tr("Actual Size"), [this] { zoomToActualSize(); });
    view->setItemKeyEquivalent(actual, "9", ctrl);
    view->addSeparator();
    const size_t fullScreen = view->addItem(tr("Enter Full Screen"), [this] { toggleFullScreen(); });
    view->setItemKeyEquivalent(fullScreen, "f", 0);
    view->onAboutToShow = [this, view, image, pdf, sidebarRow, thumbnails, contents, zoomInRow, zoomOutRow, fit,
                           actual, fullScreen] {
        view->setItemText(sidebarRow, m_sidebarShown ? tr("Hide Sidebar") : tr("Show Sidebar"));
        view->setItemEnabled(sidebarRow, !isWelcome());
        // A PDF's sidebar shows thumbnails or its outline.
        view->setItemEnabled(thumbnails, pdf());
        view->setItemEnabled(contents, pdf() && m_root->sidebar->hasContents());
        view->setItemChecked(thumbnails, pdf() && m_sidebarShown && !m_root->sidebar->showsContents());
        view->setItemChecked(contents, pdf() && m_sidebarShown && m_root->sidebar->showsContents());
        for (const size_t row : {zoomInRow, zoomOutRow, fit, actual})
            view->setItemEnabled(row, image() || pdf());
        view->setItemEnabled(fullScreen, !isWelcome() || isFullScreen());
        view->setItemText(fullScreen, isFullScreen() ? tr("Exit Full Screen") : tr("Enter Full Screen"));
    };

    ui::Menu* go = m_mainMenu->addSubmenu(tr("Go"));
    const size_t previous = go->addItem(tr("Previous"), [this] { step(-1); });
    go->setItemKeyEquivalent(previous, "Left", 0);
    const size_t next = go->addItem(tr("Next"), [this] { step(1); });
    go->setItemKeyEquivalent(next, "Right", 0);
    const size_t first = go->addItem(tr("First"), [this] { goTo(0); });
    go->setItemKeyEquivalent(first, "Home", 0);
    const size_t last = go->addItem(tr("Last"), [this] { goTo(count() - 1); });
    go->setItemKeyEquivalent(last, "End", 0);
    go->addSeparator();
    const size_t goToPage = go->addItem(tr("Go to Page…"), [this] { showGoToPage(); });
    go->setItemKeyEquivalent(goToPage, "g", ctrl | alt);
    go->onAboutToShow = [this, go, pdf, previous, next, first, last, goToPage] {
        const int index = currentIndex();
        go->setItemEnabled(previous, index > 0);
        go->setItemEnabled(first, index > 0);
        go->setItemEnabled(next, index >= 0 && index + 1 < count());
        go->setItemEnabled(last, index >= 0 && index + 1 < count());
        // Pages are a PDF's.
        go->setItemEnabled(goToPage, pdf());
    };

    ui::Menu* tools = m_mainMenu->addSubmenu(tr("Tools"));
    const size_t rotateLeft = tools->addItem(tr("Rotate Left"), [this] { rotate(-1); });
    tools->setItemKeyEquivalent(rotateLeft, "l", ctrl);
    const size_t rotateRight = tools->addItem(tr("Rotate Right"), [this] { rotate(1); });
    tools->setItemKeyEquivalent(rotateRight, "r", ctrl);
    tools->onAboutToShow = [this, tools, rotateLeft, rotateRight] {
        tools->setItemEnabled(rotateLeft, canRotate());
        tools->setItemEnabled(rotateRight, canRotate());
    };

    ui::Menu* windowMenu = m_mainMenu->addSubmenu(tr("Window"));
    windowMenu->setRole(ui::MenuRole::Windows);
    windowMenu->addItem(tr("Minimize"), [this] { m_window->minimize(); });
    windowMenu->addItem(trc("window command", "Zoom"),
                        [this] { m_window->setMaximized(!m_window->states().maximized); });

    ui::Menu* help = m_mainMenu->addSubmenu(tr("Help"));
    help->setRole(ui::MenuRole::Help);
    const size_t shortcuts = help->addItem(tr("Keyboard Shortcuts"), [this] { showShortcuts(); });
    help->setItemKeyEquivalent(shortcuts, "?", ctrl);

    m_window->setMainMenu(m_mainMenu.get());
}

void PreviewWindow::bindShortcuts()
{
    ui::Window& window = *m_window;
    const uint32_t none = ui::NoModifier;
    const uint32_t ctrl = ui::ControlModifier;
    const uint32_t shift = ui::ShiftModifier;
    window.addShortcut(none, ui::key::Left, [this] { step(-1); });
    window.addShortcut(none, ui::key::Right, [this] { step(1); });
    window.addShortcut(none, ui::key::PageUp, [this] { pageStep(-1); });
    window.addShortcut(none, ui::key::PageDown, [this] { pageStep(1); });
    window.addShortcut(shift, ui::key::Space, [this] { pageStep(-1); });
    window.addShortcut(none, ui::key::Space, [this] { pageStep(1); });
    window.addShortcut(none, ui::key::Home, [this] { goTo(0); });
    window.addShortcut(none, ui::key::End, [this] { goTo(count() - 1); });
    window.addShortcut(ctrl, ui::key::letter('='), [this] { zoomIn(); });
    window.addShortcut(ctrl, ui::key::letter('+'), [this] { zoomIn(); });
    window.addShortcut(ctrl | shift, ui::key::letter('+'), [this] { zoomIn(); });
    window.addShortcut(ctrl, ui::key::letter('-'), [this] { zoomOut(); });
    window.addShortcut(ctrl, ui::key::letter('0'), [this] { zoomToFit(); });
    window.addShortcut(ctrl, ui::key::letter('9'), [this] { zoomToActualSize(); });
    window.addShortcut(ctrl, ui::key::letter('c'), [this] { copy(); });
    window.addShortcut(ctrl, ui::key::letter('a'), [this] { selectAll(); });
    window.addShortcut(ctrl, ui::key::letter('f'), [this] { showFind(); });
    window.addShortcut(ctrl, ui::key::letter('g'), [this] { findNext(false); });
    window.addShortcut(ctrl | shift, ui::key::letter('g'), [this] { findNext(true); });
    window.addShortcut(ctrl | ui::AltModifier, ui::key::letter('g'), [this] { showGoToPage(); });
    window.addShortcut(ctrl, ui::key::letter('l'), [this] { rotate(-1); });
    window.addShortcut(ctrl, ui::key::letter('r'), [this] { rotate(1); });
    window.addShortcut(none, ui::key::Delete, [this] { moveToTrash(); });
    window.addShortcut(ctrl, ui::key::BackSpace, [this] {
        if (ui::commandKeys())
            moveToTrash();
    });
    window.addShortcut(ctrl, ui::key::letter('o'), [this] { openDialog(); });
    window.addShortcut(ctrl, ui::key::letter('w'), [this] { close(); });
    window.addShortcut(ctrl, ui::key::letter('q'), [this] { m_application.quit(); });
    // Ctrl+? whichever way the keyboard makes the question mark.
    window.addShortcut(ctrl | shift, '?', [this] { showShortcuts(); });
    window.addShortcut(ctrl, '?', [this] { showShortcuts(); });
    window.addShortcut(ctrl | shift, '/', [this] { showShortcuts(); });
    // F and F11, as in the Video Player.
    window.addShortcut(none, ui::key::letter('f'), [this] { toggleFullScreen(); });
    window.addShortcut(none, ui::key::F1 + 10, [this] { toggleFullScreen(); });
    window.addShortcut(none, ui::key::Escape, [this] {
        if (isFullScreen())
            toggleFullScreen();
    });
}
