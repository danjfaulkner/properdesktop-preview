// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "previewsettings.h"

#include "base/signal.h"
#include "base/thread_pool.h"
#include "base/timer.h"
#include "ui/geometry.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace properdesktop::ui {
class AboutDialog;
class Animation;
class FileChooser;
class Menu;
class Toast;
class View;
class Window;
struct ImageInfo;
}

class Application;
class ImagePage;
class PdfPage;
class PdfRequest;
class ShortcutsDialog;
class ImageSequence;
class Sidebar;

// A window of one document, or of a sequence of images: the header, the
// sidebar, the content under them, its keys and its menus.
class PreviewWindow
{
public:
    static constexpr double kHeaderHeight = 40;
    static constexpr double kMinimumContentWidth = 480;
    static constexpr double kMinimumContentHeight = 360;

    explicit PreviewWindow(Application& application);
    ~PreviewWindow();

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    properdesktop::ui::Window& window() const { return *m_window; }
    bool isWelcome() const { return !m_sequence; }
    // The file on screen; empty on the welcome page.
    std::string currentPath() const;
    // The shown file's place in the sequence, from 0; -1 on the welcome page.
    int currentIndex() const;
    int count() const;
    DocumentKind kind() const { return m_kind; }

    // One image: it and the other images of its folder. A PDF: its pages.
    // Several files, or a file that is neither: exactly those, in this order.
    void show(const std::vector<std::string>& paths);
    // Shows and activates the window; a window opening on a file maps once
    // the file is ready, at the size it takes from it.
    void raise(const std::string& activationToken);

    // Images or pages.
    void step(int delta);
    void goTo(int index);
    // An image, or a screenful of a PDF.
    void pageStep(int delta);
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToActualSize();
    bool sidebarShown() const { return m_sidebarShown; }
    // 0 to 1 as the sidebar slides in, the content making room for it.
    double sidebarReveal() const { return m_sidebarReveal; }
    void toggleSidebar();
    bool isFullScreen() const;
    void toggleFullScreen();
    // The image and its file's URL, or the PDF's selected text.
    void copy();
    void selectAll();
    void showFind();
    void findNext(bool backwards);
    void showGoToPage();
    void setShowsContents(bool contents);
    // A JPEG or PNG that may be written, still.
    bool canRotate() const;
    // Quarters clockwise; -1 turns left.
    void rotate(int delta);
    void showInFiles();
    void moveToTrash();
    void openDialog();
    void close();

    ImagePage* imagePage() const;
    PdfPage* pdfPage() const;
    Sidebar* sidebar() const;
    // The header's "3 of 24", or a PDF's "Page 5 of 120" where it shows, and file name.
    std::string positionText() const;
    std::string titleText() const;
    double chromeOpacity() const { return m_chromeOpacity; }
    properdesktop::ui::View* sidebarButton() const;
    properdesktop::ui::View* zoomInButton() const;
    // The header's previous arrow and place label, in the window.
    bool showsArrows() const;
    properdesktop::ui::RectF previousArrowGeometry() const;
    properdesktop::ui::RectF positionGeometry() const;
    // The file name, and a PDF's page text after it, in the window: the
    // name's drawn end, whether it is elided, and the two baselines.
    properdesktop::ui::RectF titleGeometry() const;
    properdesktop::ui::RectF pageTextGeometry() const;
    double titleTextEnd() const;
    bool titleElided() const;
    double titleBaseline() const;
    double pageTextBaseline() const;
    bool hasShownToast() const { return m_toast != nullptr; }

private:
    class HeaderBar;
    class HeaderRow;
    class Root;
    friend class Root;

    void buildMainMenu();
    void bindShortcuts();
    void retranslate();
    void shown();
    void pdfStateChanged();
    void map();
    void sequenceChanged();
    void preloadNeighbours();
    void turnWritten(const std::string& path, int error);
    void currentWritten();
    void updateHeader();
    void updateTitle();
    void updateBackground();
    void openContextMenu(properdesktop::ui::PointF position);
    void openPdfContextMenu(properdesktop::ui::PointF position, const std::string& uri);
    // The name of what is shown, for the header and the title.
    std::string shownName() const;
    void showAbout();
    void showShortcuts();
    // The full screen header: it fades out two seconds after the pointer stops.
    void poke();
    void updateChrome();
    // The sidebar slides to where m_sidebarShown puts it, or is set there.
    void updateSidebarReveal(bool animate);
    properdesktop::ui::SizeF windowSizeFor(const properdesktop::ui::ImageInfo& info) const;
    properdesktop::ui::SizeF windowSizeForPage(properdesktop::ui::SizeF page) const;
    properdesktop::ui::SizeF viewportFor(const properdesktop::ui::ImageInfo& info) const;

    Application& m_application;
    // The window shows it until the window goes, so it outlives the window.
    std::unique_ptr<properdesktop::ui::Menu> m_mainMenu;
    std::unique_ptr<properdesktop::ui::Window> m_window;
    Root* m_root = nullptr;
    std::unique_ptr<ImageSequence> m_sequence;
    DocumentKind m_kind = DocumentKind::Image;
    std::unique_ptr<PdfRequest> m_jump;
    std::vector<properdesktop::base::Subscription> m_sequenceConnections;
    // The files opened that the window has not displayed yet, for the
    // recent files.
    std::set<std::string> m_opened;
    bool m_sidebarShown = false;
    double m_sidebarReveal = 0;
    double m_revealFrom = 0;
    double m_revealTo = 0;
    std::unique_ptr<properdesktop::ui::Animation> m_revealAnimation;
    // Of every kind, as last dragged in any window.
    double m_sidebarWidth;
    // The window took its size from its file.
    bool m_sized = false;
    bool m_mapped = false;
    // Shown once its first file is ready, which sizes it.
    bool m_mapPending = false;
    std::string m_activationToken;
    properdesktop::base::Job m_copy;
    std::unique_ptr<properdesktop::ui::Toast> m_toast;
    std::unique_ptr<properdesktop::ui::Menu> m_contextMenu;
    std::unique_ptr<properdesktop::ui::FileChooser> m_chooser;
    std::unique_ptr<properdesktop::ui::AboutDialog> m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
    bool m_pointerActive = true;
    double m_chromeOpacity = 1;
    double m_chromeFrom = 1;
    double m_chromeTo = 1;
    std::unique_ptr<properdesktop::ui::Animation> m_chrome;
    properdesktop::base::Timer m_idle;
    std::vector<properdesktop::base::Subscription> m_subscriptions;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};
