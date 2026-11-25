// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "application.h"
#include "cards.h"
#include "imagepage.h"
#include "passworddialog.h"
#include "pdfdocument.h"
#include "pdfpage.h"
#include "pdfview.h"
#include "previewsettings.h"
#include "previewwindow.h"
#include "sidebar.h"
#include "testutil.h"
#include "waitfor.h"

#include "base/elapsed_timer.h"
#include "ui/application.h"
#include "ui/clipboard.h"
#include "ui/keys.h"
#include "ui/test_input.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace properdesktop::ui;
using namespace std::chrono_literals;

namespace {

// Three pages of 300 x 200. The first: a red square at its top left, "Hello
// Proper Desktop", a link to page 3 at its top right, one to a web address at its
// bottom right and a script at its bottom left. An outline: Start, End.
std::string samplePdf()
{
    const std::string page = "/Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Resources << /Font << /F1 9 0 R >> >>";
    return pdfOf({
        "<< /Type /Catalog /Pages 2 0 R /Outlines 12 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
        "<< " + page + " /Contents 6 0 R /Annots [10 0 R 11 0 R 15 0 R] >>",
        "<< " + page + " /Contents 7 0 R >>",
        "<< " + page + " /Contents 8 0 R >>",
        pdfStream("1 0 0 rg 0 180 20 20 re f BT /F1 12 Tf 30 100 Td (Hello Proper Desktop) Tj ET"),
        pdfStream("BT /F1 12 Tf 30 100 Td (Second Proper Desktop) Tj ET"),
        pdfStream("BT /F1 12 Tf 30 100 Td (Third page) Tj ET"),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Annot /Subtype /Link /Rect [200 0 300 30] /Border [0 0 0] "
        "/A << /S /URI /URI (https://properdesktop.org) >> >>",
        "<< /Type /Annot /Subtype /Link /Rect [200 170 300 200] /Border [0 0 0] /Dest [5 0 R /XYZ 0 200 0] >>",
        "<< /Type /Outlines /First 13 0 R /Last 14 0 R /Count 2 >>",
        "<< /Title (Start) /Parent 12 0 R /Next 14 0 R /Dest [3 0 R /Fit] >>",
        "<< /Title (End) /Parent 12 0 R /Prev 13 0 R /Dest [5 0 R /Fit] >>",
        "<< /Type /Annot /Subtype /Link /Rect [0 0 100 30] /Border [0 0 0] "
        "/A << /S /URI /URI (javascript:alert\\(1\\)) >> >>",
    });
}

// As many pages, each with its number, and as many outline items.
std::string bookPdf(int pages)
{
    std::vector<std::string> objects {"<< /Type /Catalog /Pages 2 0 R /Outlines 3 0 R >>", "", "", "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"};
    std::string kids;
    const int first = 5;
    for (int i = 0; i < pages; ++i) {
        const int page = first + i * 3;
        kids += std::to_string(page) + " 0 R ";
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Contents " + std::to_string(page + 1)
                          + " 0 R /Resources << /Font << /F1 4 0 R >> >> >>");
        objects.push_back(pdfStream("BT /F1 24 Tf 40 300 Td (Page " + std::to_string(i + 1) + ") Tj ET"));
        std::string item = "<< /Title (Chapter " + std::to_string(i + 1) + ") /Parent 3 0 R /Dest [" + std::to_string(page)
                + " 0 R /Fit]";
        if (i > 0)
            item += " /Prev " + std::to_string(page - 1) + " 0 R";
        if (i + 1 < pages)
            item += " /Next " + std::to_string(page + 5) + " 0 R";
        objects.push_back(item + " >>");
    }
    objects[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>";
    objects[2] = "<< /Type /Outlines /First 7 0 R /Last " + std::to_string(first + (pages - 1) * 3 + 2) + " 0 R /Count "
            + std::to_string(pages) + " >>";
    return pdfOf(objects);
}

class PdfUiTest : public testing::Test
{
protected:
    PdfUiTest()
        : settings(directory.filePath("preview.conf"))
        , application(settings)
    {
        application.uriOpener = [this](const std::string& uri) { opened.push_back(uri); };
    }

    properdesktop::base::EventLoop& loop() { return properdesktop::ui::Application::instance()->loop(); }

    PreviewWindow& openPdf(const std::string& path)
    {
        const size_t before = application.windows().size();
        application.open({path});
        EXPECT_EQ(application.windows().size(), before + 1);
        PreviewWindow& window = *application.windows().back();
        EXPECT_TRUE(waitFor(loop(), [&] { return window.pdfPage()->isReady(); }));
        window.window().grab();
        return window;
    }

    // Frames go on, so animations and scrolling move, until done() holds.
    template<typename Done>
    bool until(PreviewWindow& window, Done done)
    {
        return waitFor(loop(), [&] {
            TestInput::presentFrame(window.window());
            return done();
        });
    }

    // Every tile in sight drawn at the zoom the view rests at.
    bool settled(PreviewWindow& window)
    {
        PdfView* view = window.pdfPage()->view();
        return until(window, [&] {
            return view->renderScale() == view->zoom() * window.window().scale() && !view->hasPendingTiles()
                    && view->sharpTileCount() > 0;
        });
    }

    PointF windowPoint(PreviewWindow& window, int page, PointF point)
    {
        PdfView* view = window.pdfPage()->view();
        return view->mapToWindow(view->viewPoint(page, point));
    }

    void press(PreviewWindow& window, uint32_t key, uint32_t modifiers = NoModifier)
    {
        TestInput::typeKey(window.window(), key, modifiers);
    }

    // The clipboard's text once it is `expected`, the copy being read on the
    // worker, or what it still is after five seconds.
    std::string copied(const std::string& expected)
    {
        std::string last;
        waitFor(loop(), [&] {
            std::optional<std::string> text;
            Clipboard::requestText([&text](std::string received) { text = std::move(received); });
            waitFor(loop(), [&] { return text.has_value(); });
            last = text.value_or(std::string());
            return last == expected;
        });
        return last;
    }

    TemporaryDir directory;
    PreviewSettings settings;
    ::Application application;
    std::vector<std::string> opened;
};

} // namespace

TEST_F(PdfUiTest, APdfOpensOnItsFirstPageFittedToTheWidth)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    EXPECT_EQ(window.kind(), DocumentKind::Pdf);
    EXPECT_EQ(window.positionText(), "Page 1 of 3");
    EXPECT_EQ(window.titleText(), "sample.pdf");
    // More than one page: the sidebar shows, numbered pages in it.
    EXPECT_TRUE(window.sidebarShown());
    EXPECT_EQ(window.sidebar()->count(), 3u);
    EXPECT_TRUE(until(window, [&] { return window.pdfPage()->document()->anyThumbnail(2) != nullptr; }));
    PdfView* view = window.pdfPage()->view();
    EXPECT_TRUE(view->isFitted());
    EXPECT_DOUBLE_EQ(view->pageRect(0).width, view->geometry().width - 2 * PdfView::kMargin);
    ASSERT_TRUE(settled(window));
    const PointF square = windowPoint(window, 0, {10, 10});
    EXPECT_EQ(window.window().grab().pixel(int(square.x), int(square.y)), Color::rgb(0xFF0000));

    // A wider window keeps the pages fitted.
    TestInput::configure(window.window(), 900, 600, {});
    window.window().grab();
    EXPECT_DOUBLE_EQ(view->pageRect(0).width, view->geometry().width - 2 * PdfView::kMargin);
}

TEST_F(PdfUiTest, KeysGoThroughThePagesAndScreenfuls)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    press(window, key::Right);
    EXPECT_EQ(view->currentPage(), 1);
    EXPECT_EQ(window.positionText(), "Page 2 of 3");
    EXPECT_EQ(window.sidebar()->current(), 1);
    EXPECT_NEAR(view->pageRect(1).y, PreviewWindow::kHeaderHeight + PdfView::kMargin, 1);
    press(window, key::End);
    EXPECT_EQ(view->currentPage(), 2);
    press(window, key::Home);
    EXPECT_EQ(view->currentPage(), 0);
    EXPECT_EQ(view->scrollPosition().y, 0);
    press(window, key::Space);
    EXPECT_NEAR(view->scrollPosition().y, view->geometry().height - PreviewWindow::kHeaderHeight - 40, 1);
    press(window, key::Space, ShiftModifier);
    EXPECT_EQ(view->scrollPosition().y, 0);

    // Two fingers scroll, and what scrolls into sight is drawn.
    const PointF middle = view->mapToWindow(view->center());
    ScrollEvent event;
    event.precise = true;
    for (int i = 0; i < 10; ++i) {
        event.deltaY = 20;
        TestInput::scroll(window.window(), middle, event);
    }
    ScrollEvent stop;
    stop.precise = true;
    stop.stop = true;
    TestInput::scroll(window.window(), middle, stop);
    EXPECT_TRUE(until(window, [&] { return view->scrollPosition().y >= 150; }));
    EXPECT_TRUE(settled(window));
}

TEST_F(PdfUiTest, ZoomKeepsThePointUnderThePointerAndDrawsSharpAtRest)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    ASSERT_TRUE(settled(window));
    const double fit = view->zoom();
    press(window, key::letter('='), ControlModifier);
    EXPECT_TRUE(until(window, [&] { return std::abs(view->zoom() - fit * 1.5) < 1e-6; }));
    EXPECT_FALSE(view->isFitted());
    EXPECT_TRUE(settled(window));
    EXPECT_DOUBLE_EQ(view->renderScale(), fit * 1.5);

    // A pinch scales what is drawn until it rests.
    const PointF pointer = windowPoint(window, 0, {150, 100});
    TestInput::movePointer(window.window(), pointer);
    const PointF at = view->mapFromWindow(pointer);
    const PointF before = {(at.x - view->pageRect(0).x) / view->zoom(), (at.y - view->pageRect(0).y) / view->zoom()};
    GestureEvent gesture;
    gesture.type = GestureEvent::Type::PinchBegin;
    TestInput::gesture(window.window(), gesture);
    gesture.type = GestureEvent::Type::PinchUpdate;
    gesture.scale = 2;
    TestInput::gesture(window.window(), gesture);
    gesture.type = GestureEvent::Type::PinchEnd;
    TestInput::gesture(window.window(), gesture);
    EXPECT_NEAR(view->zoom(), fit * 3, 1e-6);
    EXPECT_DOUBLE_EQ(view->renderScale(), fit * 1.5);
    const PointF after = {(at.x - view->pageRect(0).x) / view->zoom(), (at.y - view->pageRect(0).y) / view->zoom()};
    EXPECT_NEAR(after.x, before.x, 0.5);
    EXPECT_NEAR(after.y, before.y, 0.5);
    EXPECT_TRUE(settled(window));
    EXPECT_NEAR(view->renderScale(), fit * 3, 1e-6);

    press(window, key::letter('0'), ControlModifier);
    EXPECT_TRUE(until(window, [&] { return view->isFitted() && std::abs(view->zoom() - fit) < 1e-6; }));
    press(window, key::letter('9'), ControlModifier);
    EXPECT_TRUE(until(window, [&] { return std::abs(view->zoom() - 1) < 1e-6; }));
}

TEST_F(PdfUiTest, FindTintsTheMatchesAndStepsThroughThem)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfPage* page = window.pdfPage();
    PdfView* view = page->view();
    press(window, key::letter('f'), ControlModifier);
    ASSERT_TRUE(page->findCard()->isVisible());
    TestInput::typeText(window.window(), "properdesktop");
    ASSERT_TRUE(waitFor(loop(), [&] { return page->document()->searchFinished(); }));
    EXPECT_EQ(page->document()->matchCount(), 2);
    EXPECT_EQ(view->matchPage(), 0);
    EXPECT_EQ(page->findCard()->countText(), "1 of 2");
    press(window, key::letter('g'), ControlModifier);
    EXPECT_EQ(view->matchPage(), 1);
    EXPECT_EQ(page->findCard()->countText(), "2 of 2");
    // Round the end, back to the first.
    TestInput::typeKey(window.window(), key::Return);
    EXPECT_EQ(view->matchPage(), 0);
    press(window, key::letter('g'), ControlModifier | ShiftModifier);
    EXPECT_EQ(view->matchPage(), 1);

    TestInput::typeText(window.window(), "zz");
    ASSERT_TRUE(waitFor(loop(), [&] { return page->document()->searchFinished(); }));
    EXPECT_EQ(page->findCard()->countText(), "No matches");
    TestInput::typeKey(window.window(), key::Escape);
    EXPECT_FALSE(page->findCard()->isVisible());
    EXPECT_EQ(page->document()->matchCount(), 0);
}

TEST_F(PdfUiTest, GoToPageTakesAPageNumber)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfPage* page = window.pdfPage();
    press(window, key::letter('g'), ControlModifier | AltModifier);
    ASSERT_TRUE(page->goToPageCard()->isVisible());
    TestInput::typeText(window.window(), "9");
    TestInput::typeKey(window.window(), key::Return);
    // No such page: the card stays.
    EXPECT_TRUE(page->goToPageCard()->isVisible());
    EXPECT_EQ(page->view()->currentPage(), 0);
    TestInput::typeKey(window.window(), key::BackSpace);
    TestInput::typeText(window.window(), "3");
    TestInput::typeKey(window.window(), key::Return);
    EXPECT_FALSE(page->goToPageCard()->isVisible());
    EXPECT_EQ(page->view()->currentPage(), 2);
}

TEST_F(PdfUiTest, ADragSelectsTextAndCopyCopiesIt)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    PdfDocument* document = window.pdfPage()->document();
    ASSERT_TRUE(waitFor(loop(), [&] { return document->text(0) != nullptr; }));
    const properdesktop::pdf::PageText& text = *document->text(0);
    ASSERT_GE(text.characters.size(), 14u);
    const properdesktop::base::RectF first = text.characters[0].box;
    const properdesktop::base::RectF fifth = text.characters[4].box;
    const PointF from = windowPoint(window, 0, {first.x() + 0.5, first.y() + first.height() / 2});
    const PointF to = windowPoint(window, 0, {fifth.right() - 0.5, fifth.y() + fifth.height() / 2});
    TestInput::movePointer(window.window(), from);
    TestInput::press(window.window(), from);
    TestInput::movePointer(window.window(), to);
    TestInput::release(window.window(), to);
    EXPECT_TRUE(view->hasSelection());
    press(window, key::letter('c'), ControlModifier);
    EXPECT_EQ(copied("Hello"), "Hello");

    // A double click takes the word.
    const properdesktop::base::RectF word = text.characters[8].box;
    const PointF inWord = windowPoint(window, 0, {word.x() + word.width() / 2, word.y() + word.height() / 2});
    TestInput::click(window.window(), inWord);
    TestInput::click(window.window(), inWord);
    press(window, key::letter('c'), ControlModifier);
    EXPECT_EQ(copied("ProperDesktop"), "ProperDesktop");

    press(window, key::letter('a'), ControlModifier);
    press(window, key::letter('c'), ControlModifier);
    EXPECT_EQ(copied("Hello Proper Desktop\nSecond Proper Desktop\nThird page"), "Hello Proper Desktop\nSecond Proper Desktop\nThird page");
}

TEST_F(PdfUiTest, AWrongPasswordIsAskedForAgainInPlace)
{
    application.open({PREVIEW_SOURCE_DIR "/tests/data/protected.pdf"});
    PreviewWindow& window = *application.windows().front();
    PdfPage* page = window.pdfPage();
    ASSERT_TRUE(waitFor(loop(), [&] { return page->asksPassword(); }));
    PasswordDialog* dialog = page->passwordDialog();
    TestInput::typeText(*dialog->window(), "wrong");
    TestInput::typeKey(*dialog->window(), key::Return);
    ASSERT_TRUE(waitFor(loop(), [&] { return page->document()->state() == PdfDocument::State::WrongPassword; }));
    EXPECT_EQ(page->passwordDialog(), dialog);
    EXPECT_TRUE(page->asksPassword());
    TestInput::typeText(*dialog->window(), "secret");
    TestInput::typeKey(*dialog->window(), key::Return);
    ASSERT_TRUE(waitFor(loop(), [&] { return page->isReady(); }));
    EXPECT_TRUE(waitFor(loop(), [&] { return !page->asksPassword(); }));
}

TEST_F(PdfUiTest, LinksGoToTheirPageOrOpenOnlyWebAndMail)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    ASSERT_TRUE(waitFor(loop(), [&] { return window.pdfPage()->document()->links(0) != nullptr; }));
    TestInput::click(window.window(), windowPoint(window, 0, {250, 15}));
    EXPECT_TRUE(waitFor(loop(), [&] { return view->currentPage() == 2; }));

    press(window, key::Home);
    TestInput::click(window.window(), windowPoint(window, 0, {250, 185}));
    EXPECT_EQ(opened, (std::vector<std::string> {"https://properdesktop.org"}));
    TestInput::click(window.window(), windowPoint(window, 0, {50, 185}));
    EXPECT_EQ(opened.size(), 1u);
}

TEST_F(PdfUiTest, TheContentsGoWhereTheyPoint)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    Sidebar* sidebar = window.sidebar();
    ASSERT_TRUE(waitFor(loop(), [&] { return sidebar->hasContents(); }));
    window.setShowsContents(true);
    EXPECT_TRUE(sidebar->showsContents());
    ASSERT_EQ(sidebar->contentsRowCount(), 2u);
    EXPECT_EQ(sidebar->contentsTitle(1), "End");
    sidebar->activateContentsRow(1);
    EXPECT_TRUE(waitFor(loop(), [&] { return window.pdfPage()->view()->currentPage() == 2; }));
}

// The first page is drawn before the outline and the thumbnails are read.
TEST_F(PdfUiTest, ALargeDocumentShowsItsFirstPageAtOnce)
{
    const std::string path = writeFile(directory.filePath("book.pdf"), bookPdf(2000));
    properdesktop::base::ElapsedTimer timer;
    timer.start();
    application.open({path});
    PreviewWindow& window = *application.windows().front();
    PdfPage* page = window.pdfPage();
    ASSERT_TRUE(until(window, [&] { return page->isReady() && page->view()->sharpTileCount() > 0; }));
    const int64_t firstPage = timer.elapsedMs();
    EXPECT_EQ(window.positionText(), "Page 1 of 2000");
    EXPECT_LT(firstPage, 1000);
    ASSERT_TRUE(waitFor(loop(), [&] { return window.sidebar()->hasContents(); }));
    // Only what is in sight and near it is drawn.
    EXPECT_LT(page->view()->tileCount(), 20u);
}

// Zooming out from 800% asks for nothing at 800% while the zoom moves, and
// once the new tiles are in, those of the old zoom are gone.
TEST_F(PdfUiTest, AMovingZoomAsksForNoTilesAndOldOnesGoOnceSharp)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    view->zoomTo(8, view->center(), false);
    ASSERT_TRUE(settled(window));
    const double high = view->renderScale();
    ASSERT_NEAR(high, 8 * window.window().scale(), 1e-9);
    const size_t asked = view->tilesAskedAt(high);
    view->zoomToFit();
    ASSERT_TRUE(until(window, [&] { return view->isFitted() && std::abs(view->zoom() - view->fitZoom()) < 1e-9; }));
    ASSERT_TRUE(settled(window));
    EXPECT_EQ(view->tilesAskedAt(high), asked);
    EXPECT_GT(view->tilesAskedAt(view->renderScale()), 0u);
    EXPECT_EQ(view->tileCount(), view->sharpTileCount());
}

// A double click in Chinese takes a word, not the run up to a space.
TEST_F(PdfUiTest, ADoubleClickInChineseTakesAWord)
{
    const std::string chinese = pdfOf({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
        pdfStream("BT /F1 24 Tf 20 100 Td <6211559C6B224E2D65873002> Tj ET"),
        "<< /Type /Font /Subtype /Type0 /BaseFont /STSong-Light /Encoding /UniGB-UCS2-H /DescendantFonts [<< /Type "
        "/Font /Subtype /CIDFontType0 /BaseFont /STSong-Light /CIDSystemInfo << /Registry (Adobe) /Ordering (GB1) "
        "/Supplement 2 >> /FontDescriptor << /Type /FontDescriptor /FontName /STSong-Light /Flags 4 /FontBBox [0 -200 "
        "1000 900] /ItalicAngle 0 /Ascent 880 /Descent -120 /CapHeight 700 /StemV 80 >> >>] >>",
    });
    PreviewWindow& window = openPdf(writeFile(directory.filePath("chinese.pdf"), chinese));
    PdfDocument* document = window.pdfPage()->document();
    ASSERT_TRUE(waitFor(loop(), [&] { return document->text(0) != nullptr; }));
    const properdesktop::pdf::PageText& text = *document->text(0);
    ASSERT_EQ(text.text(0, text.characters.size()), "我喜欢中文。");
    const properdesktop::base::RectF box = text.characters[1].box;
    const PointF inWord = windowPoint(window, 0, {box.x() + box.width() / 2, box.y() + box.height() / 2});
    TestInput::click(window.window(), inWord);
    TestInput::click(window.window(), inWord);
    press(window, key::letter('c'), ControlModifier);
    EXPECT_EQ(copied("喜欢"), "喜欢");
}

// Held below the view, a selecting drag scrolls on and the selection follows.
TEST_F(PdfUiTest, ADragPastTheBottomScrollsAndSelectsOn)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    PdfDocument* document = window.pdfPage()->document();
    ASSERT_TRUE(waitFor(loop(), [&] { return document->text(0) != nullptr; }));
    const properdesktop::base::RectF first = document->text(0)->characters[0].box;
    const PointF from = windowPoint(window, 0, {first.x() + 0.5, first.y() + first.height() / 2});
    TestInput::movePointer(window.window(), from);
    TestInput::press(window.window(), from);
    const PointF below = view->mapToWindow({view->center().x, view->geometry().height + 40});
    TestInput::movePointer(window.window(), below);
    EXPECT_TRUE(waitFor(loop(), [&] {
        document->text(1);
        document->text(2);
        return view->scrollPosition().y > 0 && view->selectionEnd().page >= 1;
    }));
    TestInput::release(window.window(), below);
    const double stopped = view->scrollPosition().y;
    runFor(loop(), 100ms);
    EXPECT_EQ(view->scrollPosition().y, stopped);
}

// A copy is read a page at a time; a newer one drops the one still reading.
TEST_F(PdfUiTest, ANewerCopyDropsALongOneStillReading)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("book.pdf"), bookPdf(2000)));
    PdfView* view = window.pdfPage()->view();
    press(window, key::letter('a'), ControlModifier);
    press(window, key::letter('c'), ControlModifier);
    view->setSelection({0, 0}, {0, 4});
    press(window, key::letter('c'), ControlModifier);
    EXPECT_EQ(copied("Page"), "Page");
    runFor(loop(), 500ms);
    EXPECT_EQ(copied("Page"), "Page");
}

// The pages run under the header and rest below it: at the top nothing of
// the header's backdrop shows, scrolled it does; a page gone to lands below.
TEST_F(PdfUiTest, PagesRestBelowTheHeaderAndScrollUnderItsBackdrop)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    ASSERT_TRUE(settled(window));
    const double header = PreviewWindow::kHeaderHeight;
    EXPECT_EQ(view->mapToWindow({}).y, 0);
    EXPECT_EQ(view->topInset(), header);
    EXPECT_EQ(view->mapToWindow(view->pageRect(0).topLeft()).y, header + PdfView::kMargin);
    const int x = int(view->mapToWindow(view->center()).x);
    const Color background = Theme::current().background;
    EXPECT_EQ(window.window().grab().pixel(x, int(header) - 1), background);

    press(window, key::Right);
    EXPECT_EQ(view->mapToWindow(view->pageRect(1).topLeft()).y, header + PdfView::kMargin);
    ASSERT_TRUE(settled(window));
    EXPECT_NE(window.window().grab().pixel(x, int(header) - 1), background);

    // On whole device pixels at twice the size too.
    window.window().setScale(2);
    press(window, key::Home);
    ASSERT_TRUE(settled(window));
    const double top = view->mapToWindow(view->pageRect(0).topLeft()).y;
    EXPECT_EQ(top, header + PdfView::kMargin);
    EXPECT_EQ(top * 2, std::round(top * 2));
}

// A PDF has no arrows; its name and page line start where an image's first arrow does.
TEST_F(PdfUiTest, APdfHeaderHasNoArrows)
{
    std::filesystem::create_directory(directory.filePath("images"));
    writePng(directory.filePath("images/a.png"), 40, 30);
    const std::string image = writePng(directory.filePath("images/b.png"), 40, 30);
    application.open({image});
    PreviewWindow& images = *application.windows().front();
    ASSERT_TRUE(waitFor(loop(), [&] { return images.imagePage()->shown().path == image && images.count() == 2; }));
    images.window().grab();
    ASSERT_TRUE(images.showsArrows());
    const RectF arrow = images.previousArrowGeometry();

    PreviewWindow& pdf = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    ASSERT_NE(&pdf, &images);
    EXPECT_FALSE(pdf.showsArrows());
    EXPECT_EQ(pdf.positionText(), "Page 1 of 3");
    // Both without the sidebar.
    ASSERT_FALSE(images.sidebarShown());
    if (pdf.sidebarShown())
        pdf.toggleSidebar();
    pdf.window().grab();
    // The name starts where an image's first arrow does, one group gap past
    // the toggle.
    EXPECT_EQ(pdf.titleGeometry().x, arrow.x);
    EXPECT_EQ(pdf.titleGeometry().x, std::round(pdf.titleGeometry().x));
}

// The page that shows the most below the header is current; what lies past
// the view's bottom does not count.
TEST_F(PdfUiTest, TheCurrentPageIsWhatShowsBelowTheHeader)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    PdfView* view = window.pdfPage()->view();
    const double inset = view->topInset();
    const double shown = view->geometry().height - inset;
    const double page = view->pageRect(0).height;
    // The first page shows 10 more than the second, which runs on past the bottom.
    const double first = (shown - PdfView::kPageGap) / 2 + 5;
    ASSERT_GT(page - (shown - first - PdfView::kPageGap), inset);
    view->setScrollPosition({0, inset + PdfView::kMargin + page - inset - first});
    EXPECT_NEAR(view->mapToWindow(view->pageRect(0).topLeft()).y + page, inset + first, 1);
    EXPECT_EQ(view->currentPage(), 0);
    view->setScrollPosition({0, view->scrollPosition().y + 10});
    EXPECT_EQ(view->currentPage(), 1);
}

// The name, then "Page 1 of 3" 8 after its drawn end on the same baseline,
// centred on the header's line, at 1x and 2x.
TEST_F(PdfUiTest, ThePdfTitleIsOneLineWithThePageAfterTheName)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    for (const double scale : {1.0, 2.0}) {
        window.window().setScale(scale);
        window.window().grab();
        const RectF title = window.titleGeometry();
        const RectF page = window.pageTextGeometry();
        EXPECT_EQ(window.positionText(), "Page 1 of 3");
        EXPECT_FALSE(window.titleElided());
        EXPECT_EQ(page.x, std::ceil(window.titleTextEnd() - title.x) + title.x + 8);
        EXPECT_EQ(page.y, title.y);
        EXPECT_DOUBLE_EQ(window.pageTextBaseline(), window.titleBaseline());
        EXPECT_EQ(std::round(window.pageTextBaseline() * scale), std::round(window.titleBaseline() * scale));
        EXPECT_NEAR(title.y + title.height / 2, PreviewWindow::kHeaderHeight / 2, 0.5);
        EXPECT_EQ(title.y * scale, std::round(title.y * scale));
    }
}

// A long name elides in the middle; the page text stays whole after it.
TEST_F(PdfUiTest, ALongPdfNameElidesBeforeThePageText)
{
    const std::string name(120, 'n');
    PreviewWindow& window = openPdf(writeFile(directory.filePath(name + ".pdf"), samplePdf()));
    window.window().grab();
    EXPECT_TRUE(window.titleElided());
    EXPECT_EQ(window.positionText(), "Page 1 of 3");
    const RectF title = window.titleGeometry();
    const RectF page = window.pageTextGeometry();
    EXPECT_EQ(page.x, std::ceil(window.titleTextEnd() - title.x) + title.x + 8);
    // Whole, and inside the row before the zoom buttons.
    EXPECT_LT(page.right(), window.window().size().width - 3 * 32);
}

// A page's thumbnail is 6 from its plate's sides and top, the number 4 under
// the thumbnail and 6 above the plate's bottom, at 1x and 2x.
TEST_F(PdfUiTest, PageThumbnailsKeepOneGapToTheirPlate)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    Sidebar* sidebar = window.sidebar();
    ASSERT_TRUE(window.sidebarShown());
    for (const double scale : {1.0, 2.0}) {
        window.window().setScale(scale);
        window.window().grab();
        const RectF row = sidebar->rowGeometry(1);
        const RectF thumbnail = sidebar->pageThumbnailGeometry(1, scale);
        const RectF number = sidebar->pageNumberGeometry(1);
        ASSERT_GT(row.height, 0);
        // The plate is 9 in from the sidebar's sides.
        const double plateLeft = row.x + 9;
        const double plateRight = row.right() - 9;
        EXPECT_EQ(thumbnail.x - plateLeft, 6);
        EXPECT_EQ(plateRight - thumbnail.right(), 6);
        EXPECT_EQ(thumbnail.y - row.y, 6);
        EXPECT_EQ(thumbnail.width, 160);
        EXPECT_EQ(number.y - thumbnail.bottom(), 4);
        EXPECT_EQ(row.bottom() - number.bottom(), 6);
        // On whole device pixels, as many as the render has.
        EXPECT_EQ(thumbnail.width * scale, std::round(thumbnail.width * scale));
        EXPECT_EQ(thumbnail.height * scale, std::round(thumbnail.height * scale));
    }
    EXPECT_TRUE(until(window, [&] { return window.pdfPage()->document()->anyThumbnail(1) != nullptr; }));
    const properdesktop::ui::RasterImage* image = window.pdfPage()->document()->anyThumbnail(1);
    const RectF thumbnail = sidebar->pageThumbnailGeometry(1, window.window().scale());
    EXPECT_EQ(image->width(), int(std::round(thumbnail.width * window.window().scale())));
    EXPECT_EQ(image->height(), int(std::round(thumbnail.height * window.window().scale())));
}

namespace {

// Drags the sidebar's edge to `x` in the window.
void dragSidebarEdge(PreviewWindow& window, double x)
{
    const double edge = window.sidebar()->geometry().width;
    TestInput::movePointer(window.window(), {edge, 200});
    TestInput::press(window.window(), {edge, 200});
    TestInput::movePointer(window.window(), {x, 200});
    TestInput::release(window.window(), {x, 200});
    window.window().grab();
}

} // namespace

// The edge moves within 140 to 400, and no further than leaves the content
// what it has beside the sidebar at the window's minimum size; the width is
// kept for the next window.
TEST_F(PdfUiTest, TheSidebarResizesWithinItsRangeAndIsKept)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    ASSERT_TRUE(window.sidebarShown());
    EXPECT_EQ(window.sidebar()->geometry().width, Sidebar::kDefaultWidth);
    const double windowWidth = window.window().size().width;
    dragSidebarEdge(window, 2000);
    EXPECT_EQ(window.sidebar()->geometry().width,
              std::min(Sidebar::kMaximumWidth, windowWidth - (480 - Sidebar::kDefaultWidth)));
    dragSidebarEdge(window, 10);
    EXPECT_EQ(window.sidebar()->geometry().width, Sidebar::kMinimumWidth);
    TestInput::configure(window.window(), 1200, 700, {});
    window.window().grab();
    dragSidebarEdge(window, 2000);
    EXPECT_EQ(window.sidebar()->geometry().width, Sidebar::kMaximumWidth);
    dragSidebarEdge(window, 250.4);
    EXPECT_EQ(window.sidebar()->geometry().width, 250);
    EXPECT_EQ(settings.sidebarWidth(0), 250);
    EXPECT_EQ(PreviewSettings(directory.filePath("preview.conf")).sidebarWidth(0), 250);

    // What hangs on its edge follows: the name at the page margin, in line
    // with the page at fit, and the content beside it.
    EXPECT_EQ(window.titleGeometry().x, 250 + PdfView::kMargin);
    PdfView* view = window.pdfPage()->view();
    EXPECT_EQ(view->mapToWindow({}).x, 250);
    EXPECT_EQ(view->mapToWindow(view->pageRect(0).topLeft()).x, 250 + PdfView::kMargin);

    application.open({writeFile(directory.filePath("other.pdf"), samplePdf())});
    PreviewWindow& other = *application.windows().back();
    ASSERT_TRUE(waitFor(loop(), [&] { return other.pdfPage()->isReady(); }));
    other.window().grab();
    EXPECT_EQ(other.sidebar()->geometry().width, 250);
}

// At another width the thumbnail keeps 6 to its plate and the number 4 and
// 6, at 1x and 2x, and is rendered again at the drawn size.
TEST_F(PdfUiTest, PageThumbnailsFollowTheSidebarsWidth)
{
    PreviewWindow& window = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    TestInput::configure(window.window(), 1000, 700, {});
    window.window().grab();
    dragSidebarEdge(window, 271);
    Sidebar* sidebar = window.sidebar();
    ASSERT_EQ(sidebar->geometry().width, 271);
    for (const double scale : {1.0, 2.0}) {
        window.window().setScale(scale);
        window.window().grab();
        const RectF row = sidebar->rowGeometry(1);
        const RectF thumbnail = sidebar->pageThumbnailGeometry(1, scale);
        const RectF number = sidebar->pageNumberGeometry(1);
        EXPECT_EQ(thumbnail.x - (row.x + 9), 6);
        EXPECT_EQ(row.right() - 9 - thumbnail.right(), 6);
        EXPECT_EQ(thumbnail.y - row.y, 6);
        EXPECT_EQ(thumbnail.width, 271 - 30);
        EXPECT_EQ(number.y - thumbnail.bottom(), 4);
        EXPECT_EQ(row.bottom() - number.bottom(), 6);
        EXPECT_EQ(thumbnail.height * scale, std::round(thumbnail.height * scale));
    }
    const double scale = window.window().scale();
    const RectF thumbnail = sidebar->pageThumbnailGeometry(1, scale);
    PdfDocument* document = window.pdfPage()->document();
    EXPECT_TRUE(until(window, [&] {
        const properdesktop::ui::RasterImage* image = document->anyThumbnail(1);
        return image && image->width() == int(std::round(thumbnail.width * scale))
                && image->height() == int(std::round(thumbnail.height * scale));
    }));
}

// With the sidebar hidden a PDF's title and a lone image's start 12 past the
// sidebar toggle, at 1x and 2x.
TEST_F(PdfUiTest, TitlesKeepRoomAfterTheSidebarToggle)
{
    const std::string image = writePng(directory.filePath("single.png"), 40, 30);
    application.open({image});
    PreviewWindow& images = *application.windows().front();
    ASSERT_TRUE(waitFor(loop(), [&] { return images.imagePage()->shown().path == image; }));
    PreviewWindow& pdf = openPdf(writeFile(directory.filePath("sample.pdf"), samplePdf()));
    if (pdf.sidebarShown())
        pdf.toggleSidebar();
    ASSERT_FALSE(images.sidebarShown());
    ASSERT_FALSE(images.showsArrows());
    for (const double scale : {1.0, 2.0}) {
        for (PreviewWindow* window : {&images, &pdf}) {
            window->window().setScale(scale);
            window->window().grab();
            const View* toggle = window->sidebarButton();
            const double toggleRight = toggle->mapToWindow({}).x + toggle->geometry().width;
            EXPECT_EQ(window->titleGeometry().x, toggleRight + 12);
            EXPECT_EQ(window->titleGeometry().x * scale, std::round(window->titleGeometry().x * scale));
        }
    }
}
