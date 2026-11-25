// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "imagesequence.h"
#include "previewsettings.h"
#include "testutil.h"
#include "waitfor.h"

#include "ui/application.h"
#include "ui/image_info.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using properdesktop::ui::Application;

TEST(ImageSequenceTest, AFolderIsItsImagesInNumericOrder)
{
    TemporaryDir directory;
    for (const char* name : {"img10.png", "img2.png", "Img1.jpg", "b.webp", ".hidden.png", "notes.txt"})
        writeFile(directory.filePath(name));
    const auto sequence = ImageSequence::folderOf(directory.filePath("img2.png"));
    EXPECT_EQ(sequence->paths(), std::vector<std::string> {directory.filePath("img2.png")});
    ASSERT_TRUE(waitFor(Application::instance()->loop(), [&] { return sequence->count() == 4; }));
    const std::vector<std::string> expected = {directory.filePath("b.webp"), directory.filePath("Img1.jpg"),
                                               directory.filePath("img2.png"), directory.filePath("img10.png")};
    EXPECT_EQ(sequence->paths(), expected);
    EXPECT_EQ(sequence->currentIndex(), 2);
}

TEST(ImageSequenceTest, AHiddenImageOpenedKeepsItsPlace)
{
    TemporaryDir directory;
    writeFile(directory.filePath("a.png"));
    writeFile(directory.filePath(".b.png"));
    const auto sequence = ImageSequence::folderOf(directory.filePath(".b.png"));
    EXPECT_TRUE(waitFor(Application::instance()->loop(), [&] { return sequence->count() == 2; }));
    EXPECT_EQ(sequence->currentPath(), directory.filePath(".b.png"));
}

TEST(ImageSequenceTest, FilesKeepTheOrderGiven)
{
    TemporaryDir directory;
    const std::string c = writeFile(directory.filePath("c.png"));
    const std::string a = writeFile(directory.filePath("a.png"));
    writeFile(directory.filePath("b.png"));
    const auto sequence = ImageSequence::files({c, a});
    EXPECT_EQ(sequence->paths(), (std::vector<std::string> {c, a}));
    EXPECT_EQ(sequence->currentPath(), c);
}

TEST(ImageSequenceTest, RemovingTheCurrentGoesToTheNextThenThePrevious)
{
    TemporaryDir directory;
    const std::string a = writeFile(directory.filePath("a.png"));
    const std::string b = writeFile(directory.filePath("b.png"));
    const std::string c = writeFile(directory.filePath("c.png"));
    const auto sequence = ImageSequence::files({a, b, c});
    sequence->setCurrentIndex(1);
    sequence->remove(b);
    EXPECT_EQ(sequence->currentPath(), c);
    sequence->remove(c);
    EXPECT_EQ(sequence->currentPath(), a);
    sequence->remove(a);
    EXPECT_TRUE(sequence->isEmpty());
    EXPECT_EQ(sequence->currentPath(), std::string());
}

TEST(ImageSequenceTest, TheFolderIsFollowed)
{
    auto& loop = Application::instance()->loop();
    TemporaryDir directory;
    const std::string a = writeFile(directory.filePath("a.png"));
    const std::string c = writeFile(directory.filePath("c.png"));
    const auto sequence = ImageSequence::folderOf(a);
    ASSERT_TRUE(waitFor(loop, [&] { return sequence->count() == 2; }));
    int written = 0;
    const auto subscription = sequence->currentWritten.connect([&written] { ++written; });

    const std::string b = writeFile(directory.filePath("b.png"));
    EXPECT_TRUE(waitFor(loop, [&] { return sequence->count() == 3; }));
    EXPECT_EQ(sequence->paths()[1], b);
    std::filesystem::remove(c);
    EXPECT_TRUE(waitFor(loop, [&] { return sequence->count() == 2; }));

    writeFile(a, "again\n");
    EXPECT_TRUE(waitFor(loop, [&] { return written > 0; }));
    EXPECT_EQ(sequence->currentPath(), a);

    // The current one gone: the next takes its place.
    std::filesystem::remove(a);
    EXPECT_TRUE(waitFor(loop, [&] { return sequence->count() == 1; }));
    EXPECT_EQ(sequence->currentPath(), b);
}

TEST(ImageSequenceTest, ABurstOfChangesArrivesOnce)
{
    auto& loop = Application::instance()->loop();
    TemporaryDir directory;
    const std::string a = writeFile(directory.filePath("a.png"));
    const auto sequence = ImageSequence::folderOf(a);
    // The first listing, which finds nothing new.
    runFor(loop, std::chrono::milliseconds(200));
    int changes = 0;
    int written = 0;
    const auto changed = sequence->changed.connect([&changes] { ++changes; });
    const auto rewritten = sequence->currentWritten.connect([&written] { ++written; });
    for (int i = 0; i < 50; ++i) {
        writeFile(directory.filePath("b" + std::to_string(i) + ".png"));
        if (i % 10 == 0)
            writeFile(a, "again " + std::to_string(i) + "\n");
    }
    EXPECT_TRUE(waitFor(loop, [&] { return sequence->count() == 51; }));
    runFor(loop, std::chrono::milliseconds(300));
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(written, 1);
}

TEST(ImageSequenceTest, GivenFilesLeaveWhenRemoved)
{
    auto& loop = Application::instance()->loop();
    TemporaryDir directory;
    const std::string a = writeFile(directory.filePath("a.png"));
    const std::string b = writeFile(directory.filePath("b.png"));
    const auto sequence = ImageSequence::files({a, b});
    writeFile(directory.filePath("c.png"));
    std::filesystem::remove(b);
    EXPECT_TRUE(waitFor(loop, [&] { return sequence->count() == 1; }));
    EXPECT_EQ(sequence->paths(), (std::vector<std::string> {a}));
}

TEST(PreviewSettingsTest, SidebarAndRecentFilesAreKept)
{
    TemporaryDir directory;
    const std::string file = directory.filePath("preview.conf");
    const std::string a = writeFile(directory.filePath("a.png"));
    const std::string b = writeFile(directory.filePath("b.png"));
    {
        PreviewSettings settings(file);
        EXPECT_FALSE(settings.sidebarShown(DocumentKind::Image));
        settings.setSidebarShown(DocumentKind::Image, true);
        settings.addRecentFile(a);
        settings.addRecentFile(b);
        settings.addRecentFile(a);
        settings.addRecentFile(directory.filePath("gone.png"));
    }
    PreviewSettings settings(file);
    EXPECT_TRUE(settings.sidebarShown(DocumentKind::Image));
    EXPECT_EQ(settings.recentFiles(), (std::vector<std::string> {a, b}));
    settings.clearRecentFiles();
    EXPECT_TRUE(PreviewSettings(file).recentFiles().empty());
}

TEST(DesktopEntryTest, ClaimsTheImageTypesAndPdf)
{
    std::ifstream file(PREVIEW_SOURCE_DIR "/properdesktop-preview.desktop");
    std::string line;
    std::string types;
    while (std::getline(file, line)) {
        if (line.starts_with("MimeType="))
            types = line.substr(9);
    }
    std::string expected;
    for (const std::string& type : properdesktop::ui::imageMimeTypes())
        expected += type + ";";
    EXPECT_EQ(types, expected + "application/pdf;");
}
