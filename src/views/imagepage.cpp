// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "imagepage.h"

#include "properdesktop/files/file_item.h"

#include "base/i18n.h"
#include "base/mime_database.h"
#include "ui/cell_layout.h"
#include "ui/icon.h"
#include "ui/image_view.h"
#include "ui/label.h"
#include "ui/progress_bar.h"
#include "ui/theme.h"
#include "ui/window.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace properdesktop;
using properdesktop::i18n::tr;
using ui::rounding::centeredStart;

static constexpr double kSwipeDistance = 60;
static constexpr double kIconSize = 96;
static constexpr double kCardSpacing = 20;

static ShownFile describe(const std::string& path)
{
    ShownFile file;
    file.path = path;
    file.name = std::filesystem::path(path).filename().string();
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (!error)
        file.size = files::convertSize(size);
    file.mimeType = base::mime::typeForFile(path);
    file.info = ui::readImageInfo(path, file.mimeType);
    return file;
}

bool ImageViewer::scroll(const ui::ScrollEvent& event)
{
    if (!event.precise || (event.modifiers & ui::ControlModifier) || isZoomed())
        return ZoomableImageView::scroll(event);
    if (event.stop) {
        m_swipe = 0;
        m_swiped = false;
        return true;
    }
    m_swipe += event.deltaX;
    if (!m_swiped && std::abs(m_swipe) >= kSwipeDistance && std::abs(event.deltaX) > std::abs(event.deltaY)) {
        m_swiped = true;
        if (onSwipe)
            onSwipe(m_swipe > 0 ? 1 : -1);
    }
    return true;
}

// The file's icon beside its name and size, as Quick Look shows a file it
// cannot preview.
class ImagePage::FileCard : public ui::View
{
public:
    FileCard()
    {
        m_icon = addChild<ui::ThemeIcon>(std::string(), kIconSize);
        m_icon->setImageProviderLookup(true);
        m_name = addChild<ui::Label>();
        m_name->setWeight(700);
        m_name->setFontSize(16);
        m_name->setElide(ui::Label::Elide::Middle);
        m_detail = addChild<ui::Label>();
    }

    void setFile(const ShownFile& file)
    {
        const std::string icon = file.mimeType.empty() ? std::string() : base::mime::iconName(file.mimeType);
        m_icon->setIconName(icon.empty() ? "application-octet-stream" : icon);
        m_name->setText(file.name);
        m_detail->setText(file.size);
        requestLayout();
    }

    ui::SizeF sizeHint() const override
    {
        const double text = std::max(m_name->sizeHint().width, m_detail->sizeHint().width);
        return {kIconSize + kCardSpacing + std::ceil(text), kIconSize};
    }

protected:
    void layout() override
    {
        const ui::Theme& theme = ui::Theme::current();
        m_name->setColor(theme.text);
        m_detail->setColor(theme.text);
        const double height = geometry().height;
        m_icon->setGeometry({0, centeredStart(height, kIconSize), kIconSize, kIconSize});
        const double x = kIconSize + kCardSpacing;
        const double width = std::max(0.0, geometry().width - x);
        const double nameHeight = m_name->sizeHint().height;
        const double detailHeight = m_detail->heightForWidth(width);
        const double y = centeredStart(height, nameHeight + 6 + detailHeight);
        m_name->setGeometry({x, y, width, nameHeight});
        m_detail->setGeometry({x, y + nameHeight + 6, width, detailHeight});
    }

private:
    ui::ThemeIcon* m_icon;
    ui::Label* m_name;
    ui::Label* m_detail;
};

static ui::ImageView* addPreloader(ui::View* parent)
{
    // Its source, size, orientation and fill mode are the viewer's: together
    // they are the image cache's key.
    auto* preloader = parent->addChild<ui::ImageView>();
    preloader->setFillMode(ui::ImageView::FillMode::PreserveAspectFit);
    preloader->setAutoTransform(true);
    preloader->setOpacity(0);
    return preloader;
}

ImagePage::ImagePage()
{
    m_viewer = addChild<ImageViewer>();
    m_viewer->setBottomCornerRadius(ui::Theme::current().windowRadius);
    m_viewer->onStatusChanged = [this] { updateStatus(); };
    m_busy = addChild<ui::BusyIndicator>();
    m_busy->setVisible(false);
    m_failed = addChild<ui::Label>(tr("This image could not be displayed."));
    m_failed->setVisible(false);
    m_card = addChild<FileCard>();
    m_card->setVisible(false);
    m_preloader = addPreloader(this);
    for (int i = 0; i < 2; ++i)
        m_ahead.push_back(addPreloader(this));
    m_aheadJobs.resize(m_ahead.size());

    // A slow decode is shown anyway, with the busy indicator.
    m_stall.setSingleShot(true);
    m_stall.setInterval(std::chrono::milliseconds(300));
    m_stall.onTimeout = [this] {
        if (m_pending)
            present(*m_pending);
    };
    m_busyDelay.setSingleShot(true);
    m_busyDelay.setInterval(std::chrono::milliseconds(300));
    m_busyDelay.onTimeout = [this] {
        m_busyElapsed = true;
        updateStatus();
    };
}

ImagePage::~ImagePage() = default;

void ImagePage::load(const std::string& path)
{
    m_keepsView = false;
    m_describe = base::ThreadPool::global().run([path] { return describe(path); },
                                                [this](ShownFile file) { stage(std::move(file)); });
}

void ImagePage::reload(const std::string& path)
{
    load(path);
    m_keepsView = true;
}

void ImagePage::preload(const std::vector<std::string>& paths)
{
    for (size_t i = 0; i < m_ahead.size(); ++i) {
        ui::ImageView* ahead = m_ahead[i];
        if (i >= paths.size()) {
            m_aheadJobs[i].cancel();
            ahead->setSource({});
            continue;
        }
        m_aheadJobs[i] = base::ThreadPool::global().run(
                [path = paths[i]] { return describe(path); },
                [this, ahead](ShownFile file) {
                    if (!decodesAhead(file.info))
                        return;
                    const ui::SizeF pixels = fittedPixels(file.info, viewport());
                    ahead->setSourceSize({pixels.width / ratio(), pixels.height / ratio()});
                    ahead->setSource(file.path);
                    requestLayout();
                });
    }
}

void ImagePage::clear()
{
    m_describe.cancel();
    for (base::Job& job : m_aheadJobs)
        job.cancel();
    for (ui::ImageView* ahead : m_ahead)
        ahead->setSource({});
    m_pending.reset();
    m_stall.stop();
    m_preloader->onStatusChanged = nullptr;
    m_preloader->setSource({});
    m_shown = {};
    m_viewer->setImage({}, {}, {}, ratio());
    m_card->setVisible(false);
    updateStatus();
}

void ImagePage::retranslate()
{
    m_failed->setText(tr("This image could not be displayed."));
    requestLayout();
}

void ImagePage::setTopInset(double inset)
{
    if (inset == m_topInset)
        return;
    m_topInset = inset;
    m_viewer->setTopInset(inset);
    requestLayout();
}

ui::SizeF ImagePage::viewport() const
{
    return {geometry().width, std::max(0.0, geometry().height - m_topInset)};
}

double ImagePage::ratio() const
{
    return window() && window()->scale() > 0 ? window()->scale() : 1;
}

bool ImagePage::decodesAhead(const ui::ImageInfo& info)
{
    return info.readable && !info.animated && info.width > 0 && info.height > 0;
}

ui::SizeF ImagePage::fittedPixels(const ui::ImageInfo& info, ui::SizeF viewport) const
{
    const double zoom = std::min({1.0, viewport.width * ratio() / info.width, viewport.height * ratio() / info.height});
    return {std::ceil(info.width * zoom), std::ceil(info.height * zoom)};
}

void ImagePage::stage(ShownFile file)
{
    if (!decodesAhead(file.info)) {
        present(std::move(file));
        return;
    }
    const ui::SizeF pixels = fittedPixels(file.info, viewportFor ? viewportFor(file.info) : viewport());
    m_pending = file;
    m_preloader->onStatusChanged = [this](ui::ImageView::Status status) {
        if (m_pending && status != ui::ImageView::Status::Loading)
            present(*m_pending);
    };
    m_preloader->setSourceSize({pixels.width / ratio(), pixels.height / ratio()});
    m_preloader->setSource(file.path);
    requestLayout();
    if (m_preloader->status() != ui::ImageView::Status::Loading)
        present(std::move(file));
    else if (!m_stall.isActive())
        m_stall.start();
}

// By value: callers pass *m_pending, which is reset below.
void ImagePage::present(ShownFile file)
{
    m_pending.reset();
    m_stall.stop();
    const ui::SizeF prepared = decodesAhead(file.info)
            ? fittedPixels(file.info, viewportFor ? viewportFor(file.info) : viewport())
            : ui::SizeF();
    const bool keepsView = m_keepsView && file.path == m_shown.path && m_viewer->isZoomed();
    const double zoom = m_viewer->zoom();
    const ui::PointF position = m_viewer->scrollPosition();
    m_keepsView = false;
    m_shown = std::move(file);
    m_shownRatio = ratio();
    m_viewer->setImage(m_shown.info.readable ? m_shown.path : std::string(), m_shown.info, prepared, ratio());
    if (keepsView)
        m_viewer->setZoomAndPosition(zoom, position);
    m_card->setVisible(!m_shown.info.readable);
    m_card->setFile(m_shown);
    updateStatus();
    m_preloader->onStatusChanged = nullptr;
    m_preloader->setSource({});
    requestLayout();
    if (onShown)
        onShown();
}

void ImagePage::updateStatus()
{
    const bool loading = showsImage() && m_viewer->isLoading();
    if (loading && !m_wasLoading) {
        m_busyElapsed = false;
        m_busyDelay.start();
    }
    if (!loading)
        m_busyDelay.stop();
    m_wasLoading = loading;
    m_busy->setVisible(loading && m_busyElapsed);
    m_busy->setRunning(loading && m_busyElapsed);
    m_failed->setVisible(showsImage() && m_viewer->hasFailed());
}

void ImagePage::layout()
{
    // On a screen of another scale the image is decoded again, one image
    // pixel to a device pixel there.
    if (showsImage() && ratio() != m_shownRatio && !m_describe.isPending() && !m_pending)
        load(m_shown.path);
    m_viewer->setGeometry(localRect());
    // What is centred is centred below the header.
    const ui::RectF rect {0, m_topInset, geometry().width, std::max(0.0, geometry().height - m_topInset)};
    m_busy->setGeometry({centeredStart(rect.width, 22), rect.y + centeredStart(rect.height, 22), 22, 22});
    const ui::SizeF failed = m_failed->sizeHint();
    m_failed->setColor(ui::Theme::current().text);
    m_failed->setGeometry({centeredStart(rect.width, failed.width), rect.y + centeredStart(rect.height, failed.height),
                           failed.width, failed.height});
    const ui::SizeF card = m_card->sizeHint();
    const double cardWidth = std::min(card.width, std::max(0.0, rect.width - 2 * kCardSpacing));
    m_card->setGeometry({centeredStart(rect.width, cardWidth), rect.y + centeredStart(rect.height, card.height),
                         cardWidth, card.height});
    m_preloader->setGeometry({0, 0, 1, 1});
    for (ui::ImageView* ahead : m_ahead)
        ahead->setGeometry({0, 0, 1, 1});
}
