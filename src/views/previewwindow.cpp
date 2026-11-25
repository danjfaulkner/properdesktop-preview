// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "previewwindow.h"
#include "application.h"
#include "imagepage.h"
#include "imagesequence.h"
#include "pdfdocument.h"
#include "pdfpage.h"
#include "pdfview.h"
#include "previewroot.h"
#include "rotation.h"
#include "turnsaver.h"
#include "shortcutsdialog.h"
#include "sidebar.h"
#include "welcomepage.h"

#include "properdesktop/files/task.h"
#include "properdesktop/files/url.h"

#include "base/event_loop.h"
#include "base/file.h"
#include "base/i18n.h"
#include "base/url.h"
#include "ui/about_dialog.h"
#include "ui/animation.h"
#include "ui/application.h"
#include "ui/clipboard.h"
#include "ui/color_provider.h"
#include "ui/file_chooser.h"
#include "ui/header_button.h"
#include "ui/image.h"
#include "ui/label.h"
#include "ui/menu.h"
#include "ui/toast.h"
#include "ui/window.h"
#include "ui/window_header.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <utility>


using namespace properdesktop;
using namespace std::chrono_literals;
using properdesktop::i18n::format;
using properdesktop::i18n::tr;

// The terminal's default window shape, at nine tenths of its size.
static constexpr ui::SizeF kWelcomeSize {684, 405};
static constexpr double kZoomStep = 1.5;

PreviewWindow::PreviewWindow(Application& application)
    : m_application(application)
    , m_window(std::make_unique<ui::Window>())
    , m_jump(std::make_unique<PdfRequest>())
{
    m_sidebarWidth = std::clamp(m_application.settings().sidebarWidth(Sidebar::kDefaultWidth), Sidebar::kMinimumWidth,
                                Sidebar::kMaximumWidth);
    m_window->setTitle(tr("Preview"));
    m_window->resize({720, 540});
    m_window->setMinimumSize({kMinimumContentWidth, kMinimumContentHeight + kHeaderHeight});
    m_window->setResizeAroundCenter(true);
    auto root = std::make_unique<Root>(*this);
    m_root = root.get();
    m_window->setContentView(std::move(root));

    m_revealAnimation = std::make_unique<ui::Animation>(m_window.get());
    m_revealAnimation->setDuration(200ms);
    m_revealAnimation->setEasing(ui::Animation::Easing::OutCubic);
    m_revealAnimation->onUpdate = [this](double value) {
        m_sidebarReveal = m_revealFrom + (m_revealTo - m_revealFrom) * value;
        m_root->requestLayout();
        m_root->requestPaint();
    };

    ImagePage* page = m_root->image;
    page->viewportFor = [this](const ui::ImageInfo& info) { return viewportFor(info); };
    page->onShown = [this] { shown(); };
    page->viewer()->onSwipe = [this](int delta) { step(delta); };
    page->viewer()->onContextMenuRequested = [this](ui::PointF position) { openContextMenu(position); };
    m_root->pdf->onStateChanged = [this] { pdfStateChanged(); };
    m_root->pdf->onDocumentReplaced = [this] { m_root->sidebar->setDocument(nullptr); };
    PdfView* pages = m_root->pdf->view();
    pages->onCurrentPageChanged = [this] { updateHeader(); };
    pages->onContextMenuRequested = [this](ui::PointF position, const std::string& uri) {
        openPdfContextMenu(position, uri);
    };
    pages->onOpenUri = [this](const std::string& uri) { m_application.openUri(uri, m_window.get()); };
    m_root->sidebar->onActivated = [this](int index) { goTo(index); };
    m_root->sidebar->onDestinationActivated = [this](const pdf::Destination& destination) {
        PdfDocument* document = m_root->pdf->document();
        if (!document)
            return;
        *m_jump = document->resolve(destination,
                                    [this](int page, ui::PointF point) { m_root->pdf->view()->goTo(page, point); });
    };
    WelcomePage* welcome = m_root->welcome;
    welcome->onOpen = [this] { openDialog(); };
    welcome->onRecentClicked = [this](const std::string& path) { m_application.open({path}); };
    welcome->onClearRecent = [this] { m_application.settings().clearRecentFiles(); };
    welcome->setRecentFiles(m_application.settings().recentFiles());
    m_subscriptions.push_back(m_application.settings().recentFilesChanged.connect(
            [this] { m_root->welcome->setRecentFiles(m_application.settings().recentFiles()); }));
    m_subscriptions.push_back(ui::Application::instance()->languageChanged.connect([this] { retranslate(); }));
    m_subscriptions.push_back(m_application.turnSaver().written.connect(
            [this](const std::string& path, int error) { turnWritten(path, error); }));

    m_window->onCloseRequested = [this] { close(); };
    m_window->onActiveChanged = [this] { updateBackground(); };
    m_window->onStatesChanged = [this] {
        const bool fullScreen = isFullScreen();
        ui::WindowHeader* header = m_root->bar->header;
        header->setMinimizeVisible(!fullScreen);
        header->setMaximizeVisible(!fullScreen);
        header->setCloseVisible(!fullScreen);
        poke();
        m_root->requestLayout();
        m_root->requestPaint();
    };

    m_idle.setSingleShot(true);
    m_idle.setInterval(2000ms);
    m_idle.onTimeout = [this] {
        m_pointerActive = false;
        updateChrome();
    };

    buildMainMenu();
    bindShortcuts();
    updateBackground();
    updateHeader();
}

PreviewWindow::~PreviewWindow() = default;

ImagePage* PreviewWindow::imagePage() const
{
    return m_root->image;
}

PdfPage* PreviewWindow::pdfPage() const
{
    return m_root->pdf;
}

Sidebar* PreviewWindow::sidebar() const
{
    return m_root->sidebar;
}

std::string PreviewWindow::positionText() const
{
    if (m_root->row->showsPage)
        return m_root->row->page->isVisible() ? m_root->row->page->text() : std::string();
    return m_root->row->position->isVisible() ? m_root->row->position->text() : std::string();
}

ui::RectF PreviewWindow::titleGeometry() const
{
    const ui::View* label = m_root->row->title;
    const ui::PointF origin = label->mapToWindow({});
    return {origin.x, origin.y, label->geometry().width, label->geometry().height};
}

ui::RectF PreviewWindow::pageTextGeometry() const
{
    const ui::View* label = m_root->row->page;
    const ui::PointF origin = label->mapToWindow({});
    return {origin.x, origin.y, label->geometry().width, label->geometry().height};
}

double PreviewWindow::titleTextEnd() const
{
    const ui::Label* label = m_root->row->title;
    const std::optional<ui::Label::LineGeometry> line = label->lineGeometry(0);
    return label->mapToWindow({}).x + (line ? line->width : 0);
}

bool PreviewWindow::titleElided() const
{
    return m_root->row->title->isElided();
}

double PreviewWindow::titleBaseline() const
{
    const ui::Label* label = m_root->row->title;
    return label->mapToWindow({}).y + label->baselineOffset();
}

double PreviewWindow::pageTextBaseline() const
{
    const ui::Label* label = m_root->row->page;
    return label->mapToWindow({}).y + label->baselineOffset();
}

ui::View* PreviewWindow::sidebarButton() const
{
    return m_root->row->sidebar;
}

ui::View* PreviewWindow::zoomInButton() const
{
    return m_root->row->zoomIn;
}

bool PreviewWindow::showsArrows() const
{
    return m_root->row->previous->isVisible();
}

ui::RectF PreviewWindow::previousArrowGeometry() const
{
    const ui::View* arrow = m_root->row->previous;
    const ui::PointF origin = arrow->mapToWindow({});
    return {origin.x, origin.y, arrow->geometry().width, arrow->geometry().height};
}

ui::RectF PreviewWindow::positionGeometry() const
{
    const ui::View* label = m_root->row->position;
    const ui::PointF origin = label->mapToWindow({});
    return {origin.x, origin.y, label->geometry().width, label->geometry().height};
}

std::string PreviewWindow::titleText() const
{
    return m_root->row->title->text();
}

std::string PreviewWindow::currentPath() const
{
    return m_sequence ? m_sequence->currentPath() : std::string();
}

int PreviewWindow::currentIndex() const
{
    if (!m_sequence)
        return -1;
    if (m_kind == DocumentKind::Pdf)
        return m_root->pdf->isReady() ? m_root->pdf->view()->currentPage() : 0;
    const std::vector<std::string>& paths = m_sequence->paths();
    const auto found = std::find(paths.begin(), paths.end(), m_root->image->shown().path);
    return found == paths.end() ? m_sequence->currentIndex() : int(found - paths.begin());
}

int PreviewWindow::count() const
{
    if (m_sequence && m_kind == DocumentKind::Pdf)
        return m_root->pdf->isReady() ? m_root->pdf->document()->pageCount() : 0;
    return m_sequence ? m_sequence->count() : 0;
}

void PreviewWindow::show(const std::vector<std::string>& paths)
{
    // Only the welcome page's size is kept.
    m_window->setRestoresSize({});
    std::error_code error;
    const bool folder = paths.size() == 1 && ImageSequence::isImage(paths.front())
            && std::filesystem::is_regular_file(paths.front(), error);
    m_sequenceConnections.clear();
    m_sequence = folder ? ImageSequence::folderOf(paths.front()) : ImageSequence::files(paths);
    m_sequenceConnections.push_back(m_sequence->changed.connect([this] { sequenceChanged(); }));
    m_sequenceConnections.push_back(m_sequence->currentWritten.connect([this] { currentWritten(); }));
    m_kind = paths.size() == 1 && PdfDocument::isPdf(paths.front()) ? DocumentKind::Pdf : DocumentKind::Image;
    m_opened = std::set<std::string>(paths.begin(), paths.end());
    m_mapPending = !m_mapped;
    if (m_kind == DocumentKind::Pdf) {
        // Until the document says how many pages it has.
        m_sidebarShown = false;
        updateSidebarReveal(false);
        m_root->image->clear();
        m_root->sidebar->setItems({});
        m_root->pdf->load(m_sequence->currentPath());
    } else {
        m_sidebarShown = m_application.settings().sidebarShown(kind());
        updateSidebarReveal(false);
        m_root->pdf->clear();
        m_root->sidebar->setItems(m_sequence->paths());
        m_root->image->load(m_sequence->currentPath());
    }
    m_root->requestLayout();
    updateHeader();
}

void PreviewWindow::raise(const std::string& activationToken)
{
    if (m_mapPending) {
        m_activationToken = activationToken;
        return;
    }
    if (!m_mapped && isWelcome()) {
        m_window->resize(kWelcomeSize);
        m_window->setRestoresSize("Welcome");
    }
    m_window->show();
    m_mapped = true;
    m_window->activate(activationToken);
}

void PreviewWindow::sequenceChanged()
{
    if (m_sequence->isEmpty()) {
        // Not from inside the sequence's own signal.
        base::EventLoop::current()->post([this, alive = std::weak_ptr<bool>(m_alive)] {
            if (alive.expired() || !m_sequence || !m_sequence->isEmpty())
                return;
            m_sequenceConnections.clear();
            m_sequence.reset();
            // The next file sizes the window again, as the first one did.
            m_sized = false;
            m_kind = DocumentKind::Image;
            m_root->image->clear();
            m_root->pdf->clear();
            m_root->sidebar->setItems({});
            m_root->requestLayout();
            updateHeader();
            updateTitle();
        });
        return;
    }
    if (m_kind == DocumentKind::Pdf)
        return;
    m_root->sidebar->setItems(m_sequence->paths());
    if (m_sequence->currentPath() != m_root->image->shown().path)
        m_root->image->load(m_sequence->currentPath());
    else
        preloadNeighbours();
    updateHeader();
}

void PreviewWindow::map()
{
    if (!m_mapPending)
        return;
    m_mapPending = false;
    m_window->show();
    m_mapped = true;
    m_window->activate(std::exchange(m_activationToken, std::string()));
}

void PreviewWindow::shown()
{
    if (!m_sized) {
        m_sized = true;
        if (!isFullScreen() && !m_window->states().maximized)
            m_window->resize(windowSizeFor(m_root->image->shown().info));
    }
    map();
    const std::string& path = m_root->image->shown().path;
    // A file still being turned shows the turns not written yet.
    if (const int unwritten = m_application.turnSaver().unwrittenTurns(path))
        m_root->image->viewer()->setQuarterTurns(unwritten);
    if (m_root->image->showsImage() && m_opened.erase(path))
        m_application.settings().addRecentFile(path);
    preloadNeighbours();
    updateHeader();
    updateTitle();
}

void PreviewWindow::pdfStateChanged()
{
    PdfPage* page = m_root->pdf;
    PdfDocument* document = page->document();
    if (!document || document->state() == PdfDocument::State::Opening)
        return;
    const bool ready = document->state() == PdfDocument::State::Ready;
    if (ready) {
        m_root->sidebar->setDocument(document);
        m_sidebarShown = m_application.settings().sidebarShown(DocumentKind::Pdf, document->pageCount() > 1);
        updateSidebarReveal(false);
        if (!m_sized) {
            m_sized = true;
            if (!isFullScreen() && !m_window->states().maximized)
                m_window->resize(windowSizeForPage(document->pageSizes().front()));
        }
        if (m_opened.erase(document->path()))
            m_application.settings().addRecentFile(document->path());
    }
    map();
    // The dialog goes over the window, mapped first.
    if (document->state() == PdfDocument::State::Locked && !page->asksPassword())
        page->askPassword();
    m_root->requestLayout();
    m_root->requestPaint();
    updateHeader();
    updateTitle();
}

void PreviewWindow::preloadNeighbours()
{
    if (m_kind == DocumentKind::Pdf)
        return;
    const int index = currentIndex();
    std::vector<std::string> ahead;
    for (const int neighbour : {index + 1, index - 1}) {
        if (neighbour >= 0 && neighbour < count())
            ahead.push_back(m_sequence->paths()[size_t(neighbour)]);
    }
    m_root->image->preload(ahead);
}

void PreviewWindow::updateHeader()
{
    HeaderRow* row = m_root->row;
    const bool document = !isWelcome();
    const bool multiple = count() > 1;
    const bool pdf = document && m_kind == DocumentKind::Pdf;
    const bool zoomable = pdf ? m_root->pdf->isReady() : document && m_root->image->showsImage();
    row->sidebar->setVisible(document);
    // A PDF goes through its pages in other ways; its arrows would step pages.
    row->previous->setVisible(multiple && !pdf);
    row->next->setVisible(multiple && !pdf);
    row->position->setVisible(multiple && !pdf);
    const int index = currentIndex();
    row->previous->setEnabled(index > 0);
    row->next->setEnabled(index + 1 < count());
    // TRANSLATORS: the shown image's place among the window's images, such as "3 of 24".
    row->position->setText(format(tr("{index} of {count}"),
                                  {{"index", std::to_string(index + 1)}, {"count", std::to_string(count())}}));
    row->showsPage = pdf && m_root->pdf->isReady();
    // TRANSLATORS: after a PDF's name on the header's line: the page most in sight and the number of pages.
    row->page->setText(format(tr("Page {page} of {count}"),
                              {{"page", std::to_string(index + 1)}, {"count", std::to_string(count())}}));
    row->title->setVisible(document);
    row->title->setText(shownName());
    row->zoomOut->setVisible(document);
    row->zoomIn->setVisible(document);
    row->zoomOut->setEnabled(zoomable);
    row->zoomIn->setEnabled(zoomable);
    row->rotate->setVisible(document && !pdf);
    row->rotate->setEnabled(canRotate());
    row->search->setVisible(pdf);
    row->search->setEnabled(pdf && m_root->pdf->isReady());
    if (m_root->sidebar->current() != index)
        m_root->sidebar->setCurrent(index);
    row->updateGroups();
    row->requestLayout();
}

std::string PreviewWindow::shownName() const
{
    if (m_kind == DocumentKind::Pdf)
        return std::filesystem::path(m_root->pdf->path()).filename().string();
    return m_root->image->shown().name;
}

void PreviewWindow::updateTitle()
{
    const std::string name = shownName();
    m_window->setTitle(isWelcome() || name.empty() ? std::string(tr("Preview")) : name);
}

void PreviewWindow::updateBackground()
{
    m_window->setBackgroundColor(m_window->isActive() ? ui::ColorId::TranslucentWindowBackground
                                                      : ui::ColorId::TranslucentWindowBackgroundInactive);
    // Views colour their text by it as they lay out.
    m_window->requestLayout();
}

ui::SizeF PreviewWindow::windowSizeFor(const ui::ImageInfo& info) const
{
    ui::SizeF area = m_window->availableSize();
    if (area.width <= 0 || area.height <= 0)
        area = {1280, 800};
    const double sidebar = m_sidebarShown ? m_sidebarWidth : 0;
    ui::SizeF content {kMinimumContentWidth, kMinimumContentHeight};
    if (info.readable && info.width > 0 && info.height > 0) {
        // One image pixel to a device pixel, shrunk into a large share of the work area.
        const double ratio = m_window->scale() > 0 ? m_window->scale() : 1;
        const double width = info.width / ratio;
        const double height = info.height / ratio;
        const double scale = std::min({1.0, (std::floor(area.width * 0.75) - sidebar) / width,
                                       (std::floor(area.height * 0.8) - kHeaderHeight) / height});
        content = {std::max(kMinimumContentWidth, std::round(width * scale)),
                   std::max(kMinimumContentHeight, std::round(height * scale))};
    } else if (info.readable) {
        // Without a size in its header the image fits this.
        content = {720, 540};
    }
    return {content.width + sidebar, content.height + kHeaderHeight};
}

// The first page at 100% beside the sidebar, at most 85% of the work area's
// height, and within its width.
ui::SizeF PreviewWindow::windowSizeForPage(ui::SizeF page) const
{
    ui::SizeF area = m_window->availableSize();
    if (area.width <= 0 || area.height <= 0)
        area = {1280, 800};
    const double sidebar = m_sidebarShown ? m_sidebarWidth : 0;
    const double width = std::min(page.width + 2 * PdfView::kMargin, std::floor(area.width * 0.9) - sidebar);
    const double height = std::min(page.height + 2 * PdfView::kMargin, std::floor(area.height * 0.85) - kHeaderHeight);
    return {std::max(kMinimumContentWidth, std::round(width)) + sidebar,
            std::max(kMinimumContentHeight, std::round(height)) + kHeaderHeight};
}

ui::SizeF PreviewWindow::viewportFor(const ui::ImageInfo& info) const
{
    if (m_sized || isFullScreen() || m_window->states().maximized)
        return m_root->image->viewport();
    const ui::SizeF size = windowSizeFor(info);
    return {size.width - (m_sidebarShown ? m_sidebarWidth : 0), size.height - kHeaderHeight};
}

void PreviewWindow::step(int delta)
{
    if (m_sequence)
        goTo(currentIndex() + delta);
}

void PreviewWindow::goTo(int index)
{
    if (!m_sequence)
        return;
    if (m_kind == DocumentKind::Pdf)
        m_root->pdf->view()->goToPage(index);
    else
        m_sequence->setCurrentIndex(index);
}

void PreviewWindow::pageStep(int delta)
{
    if (m_kind == DocumentKind::Pdf && m_sequence)
        m_root->pdf->view()->scrollScreen(delta);
    else
        step(delta);
}

void PreviewWindow::zoomIn()
{
    PdfView* pages = m_root->pdf->view();
    ImageViewer* viewer = m_root->image->viewer();
    if (m_kind == DocumentKind::Pdf && m_root->pdf->isReady())
        pages->zoomBy(kZoomStep, pages->center(), true);
    else if (m_kind == DocumentKind::Image && m_root->image->showsImage())
        viewer->zoomBy(kZoomStep, viewer->center(), true);
}

void PreviewWindow::zoomOut()
{
    PdfView* pages = m_root->pdf->view();
    ImageViewer* viewer = m_root->image->viewer();
    if (m_kind == DocumentKind::Pdf && m_root->pdf->isReady())
        pages->zoomBy(1 / kZoomStep, pages->center(), true);
    else if (m_kind == DocumentKind::Image && m_root->image->showsImage())
        viewer->zoomBy(1 / kZoomStep, viewer->center(), true);
}

void PreviewWindow::zoomToFit()
{
    if (m_kind == DocumentKind::Pdf && m_root->pdf->isReady())
        m_root->pdf->view()->zoomToFit();
    else if (m_kind == DocumentKind::Image && m_root->image->showsImage())
        m_root->image->viewer()->zoomToFit();
}

void PreviewWindow::zoomToActualSize()
{
    if (m_kind == DocumentKind::Pdf && m_root->pdf->isReady())
        m_root->pdf->view()->zoomToActualSize();
    else if (m_kind == DocumentKind::Image && m_root->image->showsImage())
        m_root->image->viewer()->zoomToActualSize();
}

void PreviewWindow::toggleSidebar()
{
    if (isWelcome())
        return;
    m_sidebarShown = !m_sidebarShown;
    m_application.settings().setSidebarShown(kind(), m_sidebarShown);
    updateSidebarReveal(true);
    updateHeader();
}

void PreviewWindow::updateSidebarReveal(bool animate)
{
    const double reveal = m_sidebarShown ? 1 : 0;
    if (!animate || ui::Application::instance()->platform() == ui::Application::Platform::Headless) {
        m_revealAnimation->stop();
        m_revealTo = reveal;
        m_sidebarReveal = reveal;
    } else if (reveal != m_revealTo || !m_revealAnimation->isRunning()) {
        m_revealFrom = m_sidebarReveal;
        m_revealTo = reveal;
        if (m_revealFrom != m_revealTo)
            m_revealAnimation->start();
    }
    m_root->requestLayout();
    m_root->requestPaint();
}

bool PreviewWindow::isFullScreen() const
{
    return m_window->states().fullScreen;
}

void PreviewWindow::toggleFullScreen()
{
    if (!isWelcome() || isFullScreen())
        m_window->setFullScreen(!isFullScreen());
}

// Image as PNG for any program to paste, as the file's own type besides, and
// the file; read and encoded on a worker.
void PreviewWindow::copy()
{
    if (m_kind == DocumentKind::Pdf) {
        m_root->pdf->view()->copy();
        return;
    }
    if (!m_root->image->showsImage())
        return;
    const ShownFile& file = m_root->image->shown();
    struct Copied
    {
        std::string png;
        std::string own;
    };
    m_copy = base::ThreadPool::global().run(
            [path = file.path, type = file.mimeType] {
                Copied copied;
                copied.own = base::file::read(path).value_or(std::string());
                if (type == "image/png")
                    copied.png = std::exchange(copied.own, std::string());
                else
                    copied.png = ui::Image::fromFile(path).encodePng();
                return copied;
            },
            [path = file.path, type = file.mimeType](Copied copied) {
                ui::MimeData data;
                if (!copied.png.empty())
                    data.setData("image/png", std::move(copied.png));
                if (!copied.own.empty())
                    data.setData(type, std::move(copied.own));
                data.setUrls({base::url::fromLocalFile(path)});
                ui::Clipboard::setMimeData(data);
            });
}

void PreviewWindow::selectAll()
{
    if (m_kind == DocumentKind::Pdf && m_root->pdf->isReady())
        m_root->pdf->view()->selectAll();
}

void PreviewWindow::showFind()
{
    if (m_kind == DocumentKind::Pdf)
        m_root->pdf->showFind();
}

void PreviewWindow::findNext(bool backwards)
{
    if (m_kind == DocumentKind::Pdf)
        m_root->pdf->findNext(backwards);
}

void PreviewWindow::showGoToPage()
{
    if (m_kind == DocumentKind::Pdf)
        m_root->pdf->showGoToPage();
}

void PreviewWindow::setShowsContents(bool contents)
{
    if (m_kind != DocumentKind::Pdf)
        return;
    if (!m_sidebarShown)
        toggleSidebar();
    m_root->sidebar->setShowsContents(contents);
}

bool PreviewWindow::canRotate() const
{
    const ShownFile& file = m_root->image->shown();
    return !isWelcome() && m_kind == DocumentKind::Image && m_root->image->showsImage() && !file.info.animated
            && rotation::canTurn(file.path, file.mimeType);
}

// The picture turns at once; the file follows on the process's save thread.
void PreviewWindow::rotate(int delta)
{
    if (!canRotate())
        return;
    const ShownFile& file = m_root->image->shown();
    ImageViewer* viewer = m_root->image->viewer();
    viewer->setQuarterTurns(viewer->quarterTurns() + delta);
    m_application.turnSaver().turn(file.path, file.mimeType, delta);
}

void PreviewWindow::turnWritten(const std::string& path, int error)
{
    if (!m_sequence)
        return;
    const std::vector<std::string>& paths = m_sequence->paths();
    if (std::find(paths.begin(), paths.end(), path) == paths.end())
        return;
    if (error) {
        if (!m_toast)
            m_toast = std::make_unique<ui::Toast>(m_window.get());
        const std::string name = std::filesystem::path(path).filename().string();
        if (error == rotation::kUnreadable) {
            m_toast->show(format(tr("Could not rotate {name}: it cannot be read as a JPEG or PNG image"),
                                 {{"name", name}}));
        } else {
            // TRANSLATORS: {reason} is the system's error text, such as "Permission denied".
            m_toast->show(format(tr("Could not rotate {name}: {reason}"),
                                 {{"name", name}, {"reason", std::strerror(error)}}));
        }
    }
    m_root->sidebar->refresh();
    // Decoded again as the file now is, which looks as it did turned; the
    // zoom and the place stay. Not while another file is on its way in.
    if (m_root->image->shown().path == path && currentPath() == path)
        m_root->image->reload(path);
}

void PreviewWindow::currentWritten()
{
    const std::string path = m_sequence->currentPath();
    if (m_kind == DocumentKind::Pdf) {
        m_root->pdf->load(path);
        return;
    }
    const TurnSaver& saver = m_application.turnSaver();
    // Our own writes: each brings its reload.
    if (saver.unwrittenTurns(path) != 0 || saver.writtenStamp(path) == TurnSaver::fileStamp(path))
        return;
    m_root->image->load(path);
}

void PreviewWindow::showInFiles()
{
    if (!isWelcome())
        m_application.showInFiles(currentPath());
}

void PreviewWindow::moveToTrash()
{
    const std::string path = m_kind == DocumentKind::Pdf ? currentPath() : m_root->image->shown().path;
    if (isWelcome() || path.empty())
        return;
    files::Task* task = files::trash({files::Url::fromLocalFile(path)});
    task->whenFinished([this, path, alive = std::weak_ptr<bool>(m_alive)](files::Task* finished) {
        if (!alive.expired() && m_sequence && finished->state() == files::Task::State::Succeeded)
            m_sequence->remove(path);
    });
}

void PreviewWindow::openDialog()
{
    m_chooser = std::make_unique<ui::FileChooser>(m_window.get());
    m_chooser->setMode(ui::FileChooser::Mode::OpenFiles);
    m_chooser->setTitle(tr("Open"));
    ui::FileChooser::Filter images;
    images.name = tr("Images");
    images.mimeTypes = ui::imageMimeTypes();
    ui::FileChooser::Filter documents;
    documents.name = tr("PDF Documents");
    documents.mimeTypes = {"application/pdf"};
    ui::FileChooser::Filter all;
    all.name = tr("Images and PDF Documents");
    all.mimeTypes = images.mimeTypes;
    all.mimeTypes.push_back("application/pdf");
    m_chooser->setFilters({all, images, documents});
    if (!isWelcome())
        m_chooser->setCurrentFolder(std::filesystem::path(currentPath()).parent_path().string());
    m_chooser->onAccepted = [this](const std::vector<std::string>& paths, const std::string&) {
        m_application.open(paths);
    };
    m_chooser->open();
}

void PreviewWindow::close()
{
    m_application.releaseWindow(this);
}

void PreviewWindow::openContextMenu(ui::PointF position)
{
    m_contextMenu = std::make_unique<ui::Menu>();
    m_contextMenu->addItem(tr("Copy"), [this] { copy(); });
    m_contextMenu->addSeparator();
    m_contextMenu->addItem(tr("Show in Files"), [this] { showInFiles(); });
    m_contextMenu->addItem(tr("Move to Trash"), [this] { moveToTrash(); });
    m_contextMenu->popup(m_window.get(), position);
}

void PreviewWindow::openPdfContextMenu(ui::PointF position, const std::string& uri)
{
    m_contextMenu = std::make_unique<ui::Menu>();
    if (!uri.empty()) {
        m_contextMenu->addItem(tr("Copy Link"), [uri] { ui::Clipboard::setText(uri); });
        m_contextMenu->addSeparator();
    }
    const size_t copyRow = m_contextMenu->addItem(tr("Copy"), [this] { copy(); });
    m_contextMenu->setItemEnabled(copyRow, m_root->pdf->view()->hasSelection());
    m_contextMenu->addItem(tr("Select All"), [this] { selectAll(); });
    m_contextMenu->popup(m_window.get(), position);
}

void PreviewWindow::showAbout()
{
    if (!m_about) {
        m_about = std::make_unique<ui::AboutDialog>(m_window.get());
        m_about->setName(tr("Preview"));
        m_about->setDescription(tr("View images and PDF documents"));
        m_about->setIconSource("properdesktop-preview");
        m_about->onLinkActivated = [this](const std::string& link) {
            m_application.openUri(link, m_about->window() ? m_about->window() : m_window.get());
        };
    }
    m_about->show();
}

void PreviewWindow::showShortcuts()
{
    if (!m_shortcuts)
        m_shortcuts = std::make_unique<ShortcutsDialog>(m_window.get());
    m_shortcuts->show();
}

void PreviewWindow::poke()
{
    m_pointerActive = true;
    m_idle.start();
    updateChrome();
}

void PreviewWindow::updateChrome()
{
    const double target = !isFullScreen() || isWelcome() || m_pointerActive ? 1 : 0;
    if (target == m_chromeTo)
        return;
    HeaderBar* bar = m_root->bar;
    const auto apply = [this, bar](double opacity) {
        m_chromeOpacity = opacity;
        bar->setOpacity(opacity);
        bar->setVisible(opacity > 0);
    };
    m_chromeFrom = m_chromeOpacity;
    m_chromeTo = target;
    if (!m_chrome) {
        m_chrome = std::make_unique<ui::Animation>(m_window.get());
        m_chrome->setDuration(200ms);
        m_chrome->onUpdate = [this, apply](double value) { apply(m_chromeFrom + (m_chromeTo - m_chromeFrom) * value); };
    }
    m_chrome->stop();
    if (ui::Application::instance()->platform() == ui::Application::Platform::Headless)
        apply(target);
    else
        m_chrome->start();
}

void PreviewWindow::retranslate()
{
    if (m_contextMenu && m_contextMenu->isOpen())
        m_contextMenu->close();
    buildMainMenu();
    m_root->welcome->retranslate();
    m_root->image->retranslate();
    m_root->pdf->retranslate();
    updateHeader();
    updateTitle();
}
