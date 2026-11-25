// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/thread_pool.h"
#include "base/timer.h"
#include "ui/image_info.h"
#include "ui/view.h"
#include "ui/zoomable_image_view.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace properdesktop::ui {
class BusyIndicator;
class ImageView;
class Label;
}

// A file as the page shows it, read on a worker.
struct ShownFile
{
    std::string path;
    std::string name;
    std::string mimeType;
    std::string size;
    properdesktop::ui::ImageInfo info;
};

// The zoomable image, and on a fitted one a horizontal two-finger swipe
// that steps once per gesture.
class ImageViewer : public properdesktop::ui::ZoomableImageView
{
public:
    // -1 for the previous image, 1 for the next.
    std::function<void(int step)> onSwipe;

protected:
    bool scroll(const properdesktop::ui::ScrollEvent& event) override;

private:
    double m_swipe = 0;
    bool m_swiped = false;
};

// The current file under the header. An image replaces the one shown once
// it is decoded at the size it is shown at, so the step that brings it
// changes everything in one frame; its neighbours are decoded ahead. A file
// that is not an image gets a card with its icon, name and size.
class ImagePage : public properdesktop::ui::View
{
public:
    ImagePage();
    ~ImagePage() override;

    // Reads the file's header on a worker and decodes it to fit
    // viewportFor(), then shows it; a decode over 300 ms is shown as it goes,
    // with the busy indicator.
    void load(const std::string& path);
    // As load(), for the file shown, written over: the new picture takes the
    // shown one's zoom and place.
    void reload(const std::string& path);
    // Decodes these ahead, to fit the page as it is now.
    void preload(const std::vector<std::string>& paths);
    void clear();
    void retranslate();
    // The top this far down lies under the header: the image fits, and the
    // rest centres, below it.
    void setTopInset(double inset);
    // The page's size below the inset.
    properdesktop::ui::SizeF viewport() const;

    // What is on screen; it trails load() while the next image decodes.
    const ShownFile& shown() const { return m_shown; }
    bool showsImage() const { return m_shown.info.readable; }
    ImageViewer* viewer() const { return m_viewer; }

    // The page's size when it shows the image, for the first image, which
    // sizes the window.
    std::function<properdesktop::ui::SizeF(const properdesktop::ui::ImageInfo& info)> viewportFor;
    // What is shown changed, before the frame that shows it.
    std::function<void()> onShown;

protected:
    void layout() override;

private:
    class FileCard;

    void stage(ShownFile file);
    void present(ShownFile file);
    void updateStatus();
    double ratio() const;
    // The image's decode in device pixels when it fits the viewport.
    properdesktop::ui::SizeF fittedPixels(const properdesktop::ui::ImageInfo& info, properdesktop::ui::SizeF viewport) const;
    static bool decodesAhead(const properdesktop::ui::ImageInfo& info);

    ImageViewer* m_viewer;
    properdesktop::ui::BusyIndicator* m_busy;
    properdesktop::ui::Label* m_failed;
    FileCard* m_card;
    // Decode into the image cache: the image staged, and the neighbours.
    properdesktop::ui::ImageView* m_preloader;
    std::vector<properdesktop::ui::ImageView*> m_ahead;
    ShownFile m_shown;
    // The device pixel ratio the shown image was laid out for.
    double m_shownRatio = 1;
    std::optional<ShownFile> m_pending;
    properdesktop::base::Job m_describe;
    std::vector<properdesktop::base::Job> m_aheadJobs;
    properdesktop::base::Timer m_stall;
    properdesktop::base::Timer m_busyDelay;
    bool m_keepsView = false;
    bool m_busyElapsed = false;
    bool m_wasLoading = false;
    double m_topInset = 0;
};
