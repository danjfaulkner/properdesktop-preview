// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "application.h"
#include "imagepage.h"
#include "previewsettings.h"
#include "previewwindow.h"
#include "shortcutsdialog.h"
#include "sidebar.h"
#include "testutil.h"
#include "turnsaver.h"
#include "waitfor.h"

#include "base/paths.h"
#include "ui/application.h"
#include "ui/clipboard.h"
#include "ui/header_button.h"
#include "ui/image_info.h"
#include "ui/keys.h"
#include "ui/scroll_view.h"
#include "ui/test_input.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <functional>
#include <unistd.h>

using namespace properdesktop::ui;
using namespace std::chrono_literals;

namespace {

class UiTest : public testing::Test
{
protected:
    UiTest()
        : settings(directory.filePath("preview.conf"))
        , application(settings)
    {
        previousId = properdesktop::ui::Application::instance()->applicationId();
        // Window sizes are kept under the application id; one file per
        // process, as ctest runs the tests side by side.
        const std::string name = "preview-ui-test-" + std::to_string(getpid());
        properdesktop::ui::Application::instance()->setApplicationId("properdesktop-" + name);
        windowSizes = properdesktop::base::paths::properdesktopConfig(name);
        std::filesystem::remove(windowSizes);
    }

    ~UiTest() override
    {
        std::filesystem::remove(windowSizes);
        properdesktop::ui::Application::instance()->setApplicationId(previousId);
    }

    properdesktop::base::EventLoop& loop() { return properdesktop::ui::Application::instance()->loop(); }

    PreviewWindow& onlyWindow()
    {
        EXPECT_EQ(application.windows().size(), 1u);
        return *application.windows().front();
    }

    // Until the window shows the file.
    bool showing(PreviewWindow& window, const std::string& path)
    {
        return waitFor(loop(), [&] { return window.imagePage()->shown().path == path; });
    }

    void press(PreviewWindow& window, uint32_t key, uint32_t modifiers = NoModifier)
    {
        TestInput::typeKey(window.window(), key, modifiers);
    }

    TemporaryDir directory;
    PreviewSettings settings;
    ::Application application;
    std::string previousId;
    std::string windowSizes;
};

} // namespace

TEST_F(UiTest, OneImageBringsItsFolderAndSizesTheWindow)
{
    const std::string a = writePng(directory.filePath("a.png"), 1000, 500);
    const std::string b = writePng(directory.filePath("b.png"), 300, 200);
    writePng(directory.filePath("c.png"), 300, 200);
    application.open({b});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, b));
    ASSERT_TRUE(waitFor(loop(), [&] { return window.count() == 3; }));
    EXPECT_EQ(window.positionText(), "2 of 3");
    EXPECT_EQ(window.titleText(), "b.png");
    // At least 480 x 360 under the 40 px header.
    EXPECT_EQ(window.window().size(), (SizeF {480, 400}));

    press(window, key::Left);
    ASSERT_TRUE(showing(window, a));
    EXPECT_EQ(window.positionText(), "1 of 3");
    EXPECT_EQ(window.titleText(), "a.png");
    // Stepping never resizes the window.
    EXPECT_EQ(window.window().size(), (SizeF {480, 400}));
}

TEST_F(UiTest, TheFirstImageFitsThreeQuartersOfTheWorkArea)
{
    // Headless, the work area is 1280 x 800: 960 wide at most.
    const std::string path = writePng(directory.filePath("wide.png"), 1000, 500);
    application.open({path});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, path));
    EXPECT_EQ(window.window().size(), (SizeF {960, 480 + PreviewWindow::kHeaderHeight}));
}

TEST_F(UiTest, KeysStepThroughTheSequence)
{
    std::vector<std::string> paths;
    for (const char* name : {"1.png", "2.png", "3.png"})
        paths.push_back(writePng(directory.filePath(name), 40, 30));
    application.open({paths[0]});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, paths[0]));
    ASSERT_TRUE(waitFor(loop(), [&] { return window.count() == 3; }));
    press(window, key::End);
    EXPECT_TRUE(showing(window, paths[2]));
    press(window, key::Space, ShiftModifier);
    EXPECT_TRUE(showing(window, paths[1]));
    press(window, key::Home);
    EXPECT_TRUE(showing(window, paths[0]));
    press(window, key::PageDown);
    EXPECT_TRUE(showing(window, paths[1]));
    EXPECT_EQ(window.sidebar()->current(), 1);
}

TEST_F(UiTest, SeveralImagesAreExactlyThose)
{
    const std::string a = writePng(directory.filePath("a.png"), 40, 30);
    writePng(directory.filePath("b.png"), 40, 30);
    const std::string c = writePng(directory.filePath("c.png"), 40, 30);
    application.open({c, a});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, c));
    EXPECT_EQ(window.count(), 2);
    EXPECT_EQ(window.positionText(), "1 of 2");
}

TEST_F(UiTest, OtherFilesGetWindowsOfTheirOwnWithACard)
{
    const std::string image = writePng(directory.filePath("a.png"), 40, 30);
    const std::string text = writeFile(directory.filePath("notes.txt"));
    application.open({image, text});
    ASSERT_EQ(application.windows().size(), 2u);
    PreviewWindow& textWindow = *application.windows().back();
    ASSERT_TRUE(showing(textWindow, text));
    EXPECT_FALSE(textWindow.imagePage()->showsImage());
    EXPECT_EQ(textWindow.positionText(), std::string());
}

TEST_F(UiTest, AWelcomeWindowIsReusedAndAShownFileRaisesItsWindow)
{
    application.open({});
    PreviewWindow& window = onlyWindow();
    EXPECT_TRUE(window.isWelcome());
    const std::string path = writePng(directory.filePath("a.png"), 40, 30);
    application.open({path});
    EXPECT_EQ(application.windows().size(), 1u);
    ASSERT_TRUE(showing(window, path));
    application.open({path});
    EXPECT_EQ(application.windows().size(), 1u);
    application.open({});
    EXPECT_EQ(application.windows().size(), 2u);
}

TEST_F(UiTest, TheWelcomeWindowReopensAtTheSizeItWasLeftAt)
{
    // Keeps the application running once the welcome window closes.
    const std::string path = writePng(directory.filePath("a.png"), 40, 30);
    application.open({path});
    application.open({});
    ASSERT_EQ(application.windows().size(), 2u);
    PreviewWindow& welcome = *application.windows().back();
    EXPECT_TRUE(welcome.isWelcome());
    EXPECT_EQ(welcome.window().size(), (SizeF {684, 405}));
    TestInput::resizeInteractively(welcome.window(), {900, 600});
    runFor(loop(), 400ms);
    welcome.close();

    application.open({});
    ASSERT_EQ(application.windows().size(), 2u);
    EXPECT_EQ(application.windows().back()->window().size(), (SizeF {900, 600}));
}

TEST_F(UiTest, AWelcomeWindowGivenAFileLeavesTheWelcomeSizeAlone)
{
    application.open({});
    PreviewWindow& window = onlyWindow();
    const std::string path = writePng(directory.filePath("a.png"), 40, 30);
    application.open({path});
    ASSERT_TRUE(showing(window, path));
    TestInput::resizeInteractively(window.window(), {900, 600});
    runFor(loop(), 400ms);

    application.open({});
    ASSERT_EQ(application.windows().size(), 2u);
    EXPECT_EQ(application.windows().back()->window().size(), (SizeF {684, 405}));
}

TEST_F(UiTest, TheSidebarIsKeptForImages)
{
    const std::string path = writePng(directory.filePath("a.png"), 40, 30);
    application.open({path});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, path));
    window.window().grab();
    EXPECT_FALSE(window.sidebar()->isVisible());
    window.toggleSidebar();
    window.window().grab();
    EXPECT_TRUE(window.sidebar()->isVisible());
    EXPECT_TRUE(settings.sidebarShown(DocumentKind::Image));
    // The sidebar shows for itself; the toggle is a plain button either way.
    auto* toggle = static_cast<HeaderButton*>(window.sidebarButton());
    EXPECT_FALSE(toggle->isChecked());
    window.toggleSidebar();
    window.window().grab();
    EXPECT_FALSE(window.sidebar()->isVisible());
    EXPECT_FALSE(toggle->isChecked());
}

TEST_F(UiTest, CopyOffersPngTheFilesOwnTypeAndItsUrl)
{
    const std::string png = writePng(directory.filePath("a.png"), 40, 30);
    // A 4 x 3 true colour TGA, written by hand.
    std::string tga = std::string("\0\0\2", 3) + std::string(9, '\0') + std::string("\4\0\3\0\x18\0", 6);
    tga += std::string(4 * 3 * 3, '\x40');
    const std::string other = writeFile(directory.filePath("b.tga"), tga);
    application.open({png, other});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, png));

    const auto offers = [](const std::string& type) {
        const std::vector<std::string> types = Clipboard::mimeTypes();
        return std::find(types.begin(), types.end(), type) != types.end();
    };
    press(window, key::letter('c'), ControlModifier);
    ASSERT_TRUE(waitFor(loop(), [&] { return offers("image/png"); }));
    EXPECT_TRUE(offers("text/uri-list"));
    EXPECT_EQ(Clipboard::mimeTypes().size(), 2u);

    press(window, key::Right);
    ASSERT_TRUE(showing(window, other));
    press(window, key::letter('c'), ControlModifier);
    ASSERT_TRUE(waitFor(loop(), [&] { return offers("image/x-tga"); }));
    EXPECT_TRUE(offers("image/png"));
    EXPECT_TRUE(offers("text/uri-list"));
    std::string pasted;
    Clipboard::requestData("image/png", [&pasted](std::optional<std::string> data) { pasted = data.value_or(""); });
    ASSERT_TRUE(waitFor(loop(), [&] { return !pasted.empty(); }));
    const ImageInfo info = readImageInfo(writeFile(directory.filePath("pasted.png"), pasted), "image/png");
    EXPECT_EQ(info.width, 4);
    EXPECT_EQ(info.height, 3);
}

TEST_F(UiTest, ASwipeOnAFittedImageStepsOnce)
{
    std::vector<std::string> paths;
    for (const char* name : {"1.png", "2.png", "3.png"})
        paths.push_back(writePng(directory.filePath(name), 40, 30));
    application.open({paths[0]});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, paths[0]));
    ASSERT_TRUE(waitFor(loop(), [&] { return window.count() == 3; }));
    window.window().grab();
    const PointF center {240, 200};
    ScrollEvent event;
    event.precise = true;
    for (int i = 0; i < 10; ++i) {
        event.deltaX = 20;
        TestInput::scroll(window.window(), center, event);
    }
    ScrollEvent stop;
    stop.precise = true;
    stop.stop = true;
    TestInput::scroll(window.window(), center, stop);
    EXPECT_TRUE(showing(window, paths[1]));
    runFor(loop(), 100ms);
    EXPECT_EQ(window.imagePage()->shown().path, paths[1]);
}

TEST_F(UiTest, TheFullScreenHeaderFadesWhenThePointerRests)
{
    const std::string path = writePng(directory.filePath("a.png"), 40, 30);
    application.open({path});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, path));
    WindowStates states;
    states.fullScreen = true;
    TestInput::configure(window.window(), 1280, 800, states);
    // The header over the black around the image.
    EXPECT_EQ(window.window().grab().pixel(640, 20), Theme::current().background);
    EXPECT_EQ(window.chromeOpacity(), 1);
    EXPECT_TRUE(waitFor(loop(), [&] { return window.chromeOpacity() == 0; }, 3s));
    TestInput::movePointer(window.window(), {100, 100});
    EXPECT_TRUE(waitFor(loop(), [&] { return window.chromeOpacity() == 1; }));
}

// A pinch to three times over the image, which then overflows the view both
// ways, keeps the image's point under the pointer, with the sidebar shown or
// in full screen too.
TEST_F(UiTest, APinchZoomsAboutThePointer)
{
    const std::string path = writePng(directory.filePath("a.png"), 1600, 1200);
    writePng(directory.filePath("b.png"), 1600, 1200);
    application.open({path});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, path));
    ASSERT_TRUE(waitFor(loop(), [&] { return window.count() == 2; }));
    ImageViewer* viewer = window.imagePage()->viewer();
    const auto pinch = [&](PointF pointer) {
        window.window().grab();
        TestInput::movePointer(window.window(), pointer);
        const PointF anchor = viewer->mapFromWindow(pointer);
        const PointF before = viewer->imagePoint(anchor);
        const double fit = viewer->zoom();
        GestureEvent event;
        event.type = GestureEvent::Type::PinchBegin;
        TestInput::gesture(window.window(), event);
        event.type = GestureEvent::Type::PinchUpdate;
        event.scale = 3;
        TestInput::gesture(window.window(), event);
        event.type = GestureEvent::Type::PinchEnd;
        TestInput::gesture(window.window(), event);
        EXPECT_NEAR(viewer->zoom(), fit * 3, 1e-9);
        const PointF after = viewer->imagePoint(anchor);
        EXPECT_NEAR(after.x, before.x, 0.5);
        EXPECT_NEAR(after.y, before.y, 0.5);
        // Pinching in stops at the fit.
        event.type = GestureEvent::Type::PinchBegin;
        TestInput::gesture(window.window(), event);
        event.type = GestureEvent::Type::PinchUpdate;
        event.scale = 0.2;
        TestInput::gesture(window.window(), event);
        event.type = GestureEvent::Type::PinchEnd;
        TestInput::gesture(window.window(), event);
        EXPECT_NEAR(viewer->zoom(), fit, 1e-9);
    };
    pinch({300, 200});

    window.toggleSidebar();
    pinch({500, 300});

    press(window, key::Right);
    ASSERT_TRUE(showing(window, directory.filePath("b.png")));
    pinch({400, 250});

    WindowStates states;
    states.fullScreen = true;
    TestInput::configure(window.window(), 1280, 800, states);
    pinch({700, 500});
}

// A turn shows at once, the file follows, and its own write reloads it once.
TEST_F(UiTest, RotatingTurnsThePictureThenTheFile)
{
    const std::string png = writePng(directory.filePath("a.png"), 60, 30);
    std::string tga = std::string("\0\0\2", 3) + std::string(9, '\0') + std::string("\4\0\3\0\x18\0", 6);
    tga += std::string(4 * 3 * 3, '\x40');
    const std::string other = writeFile(directory.filePath("b.tga"), tga);
    application.open({png, other});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, png));
    ImageViewer* viewer = window.imagePage()->viewer();
    EXPECT_TRUE(window.canRotate());

    // Three quick presses come to one turn left.
    press(window, key::letter('r'), ControlModifier);
    EXPECT_EQ(viewer->quarterTurns(), 1);
    press(window, key::letter('r'), ControlModifier);
    press(window, key::letter('r'), ControlModifier);
    EXPECT_EQ(viewer->quarterTurns(), 3);
    ASSERT_TRUE(waitFor(loop(), [&] {
        return readImageInfo(png, "image/png").width == 30 && window.imagePage()->shown().info.width == 30
                && viewer->quarterTurns() == 0;
    }));
    EXPECT_EQ(readImageInfo(png, "image/png").height, 60);

    // The folder watch reports the save too; it brings no second reload,
    // which would fit the image again.
    viewer->zoomTo(2, viewer->center(), false);
    runFor(loop(), 400ms);
    EXPECT_TRUE(viewer->isZoomed());

    press(window, key::Right);
    ASSERT_TRUE(showing(window, other));
    EXPECT_FALSE(window.canRotate());
    press(window, key::letter('l'), ControlModifier);
    EXPECT_EQ(viewer->quarterTurns(), 0);
}

// Turns asked for in a window closed at once are written all the same.
TEST_F(UiTest, TurnsOutliveTheirWindow)
{
    const std::string png = writePng(directory.filePath("a.png"), 60, 30);
    const std::string text = writeFile(directory.filePath("notes.txt"));
    application.open({png, text});
    ASSERT_EQ(application.windows().size(), 2u);
    PreviewWindow& window = *application.windows().front();
    ASSERT_TRUE(showing(window, png));
    for (int i = 0; i < 3; ++i)
        press(window, key::letter('r'), ControlModifier);
    window.close();
    ASSERT_TRUE(waitFor(loop(), [&] { return application.windows().size() == 1 && application.turnSaver().isIdle(); }));
    // Three turns right are one left: 30 x 60.
    EXPECT_EQ(readImageInfo(png, "image/png").width, 30);
}

// The reload after a save keeps the zoom and the point under the pointer.
TEST_F(UiTest, TheReloadAfterATurnKeepsTheView)
{
    const std::string path = writePng(directory.filePath("big.png"), 1600, 1200);
    application.open({path});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, path));
    window.window().grab();
    ImageViewer* viewer = window.imagePage()->viewer();
    press(window, key::letter('r'), ControlModifier);
    const PointF at {150, 120};
    viewer->zoomTo(viewer->zoom() * 3, at, false);
    const double zoom = viewer->zoom();
    const PointF before = viewer->imagePoint(at);
    ASSERT_TRUE(waitFor(loop(), [&] {
        return application.turnSaver().isIdle() && window.imagePage()->shown().info.width == 1200
                && viewer->quarterTurns() == 0;
    }));
    EXPECT_DOUBLE_EQ(viewer->zoom(), zoom);
    const PointF after = viewer->imagePoint(at);
    EXPECT_NEAR(after.x, before.x, 0.5);
    EXPECT_NEAR(after.y, before.y, 0.5);
}

// A neighbour decoded ahead before its file was turned is not shown after.
TEST_F(UiTest, AFileTurnedWhileAwayShowsTurned)
{
    const std::string a = writeHalvesPng(directory.filePath("a.png"), 60, 30);
    const std::string b = writePng(directory.filePath("b.png"), 60, 30);
    application.open({a, b});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, a));
    press(window, key::letter('r'), ControlModifier);
    press(window, key::Right);
    ASSERT_TRUE(showing(window, b));
    ASSERT_TRUE(waitFor(loop(), [&] { return application.turnSaver().isIdle(); }));
    runFor(loop(), 200ms);
    press(window, key::Left);
    ASSERT_TRUE(showing(window, a));
    ImageViewer* viewer = window.imagePage()->viewer();
    EXPECT_EQ(viewer->quarterTurns(), 0);
    EXPECT_EQ(window.imagePage()->shown().info.width, 30);
    ASSERT_TRUE(waitFor(loop(), [&] {
        window.window().grab();
        return !viewer->isLoading();
    }));
    const Image shown = window.window().grab();
    const PointF centre = viewer->mapToWindow(viewer->center());
    // Turned clockwise: the red left half is on top.
    EXPECT_EQ(shown.pixel(int(centre.x), int(centre.y) - 10), Color::rgb(0xFF0000));
    EXPECT_EQ(shown.pixel(int(centre.x), int(centre.y) + 10), Color::rgb(0x0000FF));
}

// A failed write is told only in the windows that hold the file.
TEST_F(UiTest, AFailedTurnIsToldWhereTheFileIs)
{
    std::filesystem::create_directory(directory.filePath("one"));
    std::filesystem::create_directory(directory.filePath("two"));
    const std::string a = writePng(directory.filePath("one/a.png"), 40, 30);
    const std::string b = writePng(directory.filePath("two/b.png"), 40, 30);
    application.open({a});
    application.open({b});
    ASSERT_EQ(application.windows().size(), 2u);
    PreviewWindow& first = *application.windows().front();
    PreviewWindow& second = *application.windows().back();
    ASSERT_TRUE(showing(first, a));
    ASSERT_TRUE(showing(second, b));
    application.turnSaver().written(a, EACCES);
    EXPECT_TRUE(first.hasShownToast());
    EXPECT_FALSE(second.hasShownToast());
}

// An image fits below the header, centred there, with nothing of the
// header's backdrop; zoomed and moved up, it shows under the backdrop.
TEST_F(UiTest, AnImageFitsBelowTheHeaderAndPansUnderIt)
{
    const std::string path = writePng(directory.filePath("big.png"), 1600, 1200);
    application.open({path});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, path));
    window.window().grab();
    ImageViewer* viewer = window.imagePage()->viewer();
    const double header = PreviewWindow::kHeaderHeight;
    const SizeF size = window.window().size();
    const PointF centre = viewer->mapToWindow(viewer->center());
    EXPECT_EQ(centre.x, size.width / 2);
    EXPECT_EQ(centre.y, header + (size.height - header) / 2);
    const Color background = Theme::current().background;
    EXPECT_EQ(window.window().grab().pixel(int(centre.x), int(header) - 1), background);

    viewer->zoomTo(viewer->zoom() * 3, viewer->center(), false);
    viewer->setScrollPosition({viewer->scrollPosition().x, 400});
    EXPECT_TRUE(waitFor(loop(), [&] {
        return window.window().grab().pixel(int(centre.x), int(header) - 1) != background;
    }));
}

// The sidebar toggle draws the toolkit's sidebar glyph at its own 24 pixels,
// centred in its button, on the line of the other header buttons.
TEST_F(UiTest, TheSidebarToggleIsCentredOnTheHeaderLine)
{
    const std::string path = writePng(directory.filePath("a.png"), 40, 30);
    application.open({path});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, path));
    window.window().grab();
    View* toggle = window.sidebarButton();
    View* zoomIn = window.zoomInButton();
    const PointF toggleTop = toggle->mapToWindow({});
    const PointF zoomTop = zoomIn->mapToWindow({});
    EXPECT_EQ(toggle->geometry().size(), zoomIn->geometry().size());
    EXPECT_EQ(toggleTop.y, zoomTop.y);
    for (const double scale : {1.0, 2.0}) {
        const Rect ink = TestInput::inkRect(*toggle, scale);
        // The glyph's frame, 13 x 9 on a 24 grid with a one-pixel line.
        EXPECT_EQ(ink.width, int(14 * scale));
        EXPECT_EQ(ink.height, int(10 * scale));
        EXPECT_EQ(ink.x * 2 + ink.width, int(toggle->geometry().width * scale));
        EXPECT_EQ(ink.y * 2 + ink.height, int(toggle->geometry().height * scale));
    }
}

// For images the arrows start 9 past the sidebar's edge at any width, and
// the rows grow with it, 120 tall at 190 wide.
TEST_F(UiTest, TheImageHeaderAndRowsFollowTheSidebarsWidth)
{
    std::vector<std::string> paths;
    for (const char* name : {"1.png", "2.png"})
        paths.push_back(writePng(directory.filePath(name), 40, 30));
    application.open({paths[0]});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, paths[0]));
    ASSERT_TRUE(waitFor(loop(), [&] { return window.count() == 2; }));
    TestInput::configure(window.window(), 1000, 700, {});
    window.toggleSidebar();
    window.window().grab();
    EXPECT_EQ(window.previousArrowGeometry().x, Sidebar::kDefaultWidth + 9);
    EXPECT_EQ(window.sidebar()->rowGeometry(0).height, 120);
    const double edge = window.sidebar()->geometry().width;
    TestInput::movePointer(window.window(), {edge, 200});
    TestInput::press(window.window(), {edge, 200});
    TestInput::movePointer(window.window(), {285, 200});
    TestInput::release(window.window(), {285, 200});
    window.window().grab();
    EXPECT_EQ(window.sidebar()->geometry().width, 285);
    EXPECT_EQ(window.previousArrowGeometry().x, 285 + 9);
    EXPECT_EQ(window.sidebar()->rowGeometry(0).height, 180);
}

// Without the sidebar the header keeps one spacing: the window buttons'
// inset of 9 at the end too, 12 between groups, 8 between "3 of 24" and the
// name, the buttons centred on the header's line.
TEST_F(UiTest, TheHeaderKeepsOneSpacing)
{
    std::vector<std::string> paths;
    for (const char* name : {"1.png", "2.png"})
        paths.push_back(writePng(directory.filePath(name), 40, 30));
    application.open({paths[0]});
    PreviewWindow& window = onlyWindow();
    ASSERT_TRUE(showing(window, paths[0]));
    ASSERT_TRUE(waitFor(loop(), [&] { return window.count() == 2; }));
    TestInput::configure(window.window(), 1000, 700, {});
    ASSERT_FALSE(window.sidebarShown());
    window.window().grab();
    const auto inWindow = [](const View* view) {
        const PointF origin = view->mapToWindow({});
        return RectF {origin.x, origin.y, view->geometry().width, view->geometry().height};
    };
    const RectF toggle = inWindow(window.sidebarButton()->parent());
    const RectF tools = inWindow(window.zoomInButton()->parent());
    const RectF previous = window.previousArrowGeometry();
    const RectF position = window.positionGeometry();
    const RectF title = window.titleGeometry();
    // Close, minimize and maximize, 22 wide and 6 apart from 9.
    EXPECT_EQ(toggle.x, 9 + 3 * 22 + 2 * 6 + 12);
    EXPECT_EQ(previous.x, toggle.right() + 12);
    EXPECT_EQ(position.x, previous.x + 2 * previous.width + 12);
    EXPECT_EQ(title.x, position.right() + 8);
    EXPECT_EQ(window.window().size().width - tools.right(), 9);
    for (const RectF& rect : {toggle, tools, previous})
        EXPECT_EQ(rect.y * 2 + rect.height, PreviewWindow::kHeaderHeight);
    EXPECT_EQ(position.y, title.y);
}

// The shortcuts list spans the dialog under its title, its bar at the
// window's right edge, and takes the pointer to every edge.
TEST_F(UiTest, ShortcutsScrollFromEveryEdge)
{
    Window parent;
    parent.resize({800, 600});
    parent.setContentView(std::make_unique<View>());
    parent.show();
    ShortcutsDialog shortcuts(&parent);
    shortcuts.show();
    Window* dialog = nullptr;
    for (Window* window : properdesktop::ui::Application::instance()->windows()) {
        if (window->transientParent() == &parent && !window->isHidden())
            dialog = window;
    }
    ASSERT_TRUE(dialog);
    dialog->grab();
    ScrollView* scroll = nullptr;
    const std::function<void(View*)> collect = [&](View* view) {
        if (auto* found = dynamic_cast<ScrollView*>(view))
            scroll = found;
        for (const auto& child : view->children())
            collect(child.get());
    };
    collect(dialog->contentView());
    ASSERT_TRUE(scroll);
    const SizeF size = dialog->size();
    EXPECT_EQ(size, (SizeF {480, 42 + 600 - 44}));
    const PointF origin = scroll->mapToWindow({});
    EXPECT_EQ((RectF {origin.x, origin.y, scroll->geometry().width, scroll->geometry().height}),
              (RectF {0, 42, size.width, size.height - 42}));
    ASSERT_GT(scroll->contentSize().height, scroll->geometry().height);

    // A drag of the handle, which starts under the bar's top, at the edge.
    const PointF grab {size.width - 4, 42 + 4};
    TestInput::press(*dialog, grab);
    TestInput::movePointer(*dialog, {grab.x, grab.y + 60});
    TestInput::release(*dialog, {grab.x, grab.y + 60});
    dialog->grab();
    EXPECT_GT(scroll->scrollPosition().y, 0) << "the drag at the edge";

    ScrollEvent wheel;
    wheel.value120Y = 120;
    wheel.deltaY = 10;
    const double middle = 42 + (size.height - 42) / 2;
    for (const PointF point : {PointF {size.width - 1, middle}, PointF {4, middle},
                               PointF {size.width / 2, size.height - 4}}) {
        scroll->setScrollPosition({0, 0});
        TestInput::scroll(*dialog, point, wheel);
        dialog->grab();
        EXPECT_GT(scroll->scrollPosition().y, 0) << "the wheel at " << point.x << ", " << point.y;
    }
}
