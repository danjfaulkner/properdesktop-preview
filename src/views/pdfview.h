// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "pdfdocument.h"

#include "base/signal.h"
#include "base/timer.h"
#include "ui/raster_image.h"
#include "ui/scroll_view.h"

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace properdesktop::ui {
class Animation;
}

// The pages of a PDF in one column on the window's background, scrolled
// vertically and zoomed as an image is: Ctrl+wheel and pinch about the
// pointer, the keys about the middle. Zoom 1 is one point to a logical
// pixel; a document opens fitted to the view's width and follows it until
// the user zooms. Pages are drawn in tiles of 512 device pixels rendered on
// the document's worker at the zoom things come to rest at, the ones in
// sight first, then half a screen above and below; while a pinch or a zoom
// moves, the tiles there are scaled. At most 128 MiB of tiles stay, the
// least recently wanted going first. A drag selects text, a double click a
// word and a triple click a line; a click on a link follows it.
class PdfView : public properdesktop::ui::ScrollView
{
public:
    // Room for each page's shadow too.
    static constexpr double kMargin = 16;
    static constexpr double kPageGap = 16;
    static constexpr int kTileSize = 512;
    static constexpr size_t kTileBytes = size_t(128) * 1024 * 1024;

    PdfView();
    ~PdfView() override;

    // Ready, or null; it outlives the view.
    void setDocument(PdfDocument* document);
    PdfDocument* document() const { return m_document; }

    double zoom() const { return m_zoom; }
    double fitZoom() const;
    bool isFitted() const { return m_fitted; }
    void zoomBy(double factor, properdesktop::ui::PointF at, bool animate);
    void zoomTo(double target, properdesktop::ui::PointF at, bool animate);
    void zoomToFit() { zoomTo(fitZoom(), center(), true); }
    void zoomToActualSize() { zoomTo(1, center(), true); }
    // The middle of the area below the top inset.
    properdesktop::ui::PointF center() const
    {
        return {geometry().width / 2, m_topInset + (geometry().height - m_topInset) / 2};
    }
    // The top this far down lies under a floating header: pages rest below
    // it and scroll under it, and what is gone to lands below it.
    void setTopInset(double inset);
    double topInset() const { return m_topInset; }
    // Pages float on soft shadows; full screen shows them flat.
    void setPageShadows(bool shadows);
    // Device pixels per point that the tiles drawn last were asked at.
    double renderScale() const { return m_renderScale; }

    // The page most of the view shows, from 0.
    int currentPage() const { return m_currentPage; }
    // The page's top at the view's top.
    void goToPage(int page);
    // The point of the page as shown near the view's top.
    void goTo(int page, properdesktop::ui::PointF point);
    // A screenful down, or up.
    void scrollScreen(int direction);

    bool hasSelection() const { return m_anchor != m_focus; }
    void selectAll();
    void clearSelection();
    // The selected text to the clipboard, read on the worker.
    void copy();
    void setSelection(TextPosition anchor, TextPosition focus);
    TextPosition selectionStart() const { return std::min(m_anchor, m_focus); }
    TextPosition selectionEnd() const { return std::max(m_anchor, m_focus); }

    // The match tinted the strongest, scrolled into view; -1 for none.
    void setCurrentMatch(int page, int index);
    int matchPage() const { return m_matchPage; }
    int matchIndex() const { return m_matchIndex; }

    // In the view's coordinates.
    properdesktop::ui::RectF pageRect(int page) const;
    properdesktop::ui::PointF viewPoint(int page, properdesktop::ui::PointF point) const;
    // How many tiles are kept, and of those at the current render scale.
    size_t tileCount() const { return m_tiles.size(); }
    size_t sharpTileCount() const;
    bool hasPendingTiles() const;
    // Tiles asked of the worker at a scale, in device pixels per point.
    size_t tilesAskedAt(double scale) const;

    std::function<void()> onCurrentPageChanged;
    std::function<void()> onSelectionChanged;
    // A right press: the link under it, if any.
    std::function<void(properdesktop::ui::PointF windowPosition, const std::string& uri)> onContextMenuRequested;
    // An http, https or mailto link clicked.
    std::function<void(const std::string& uri)> onOpenUri;

protected:
    void layout() override;
    bool scroll(const properdesktop::ui::ScrollEvent& event) override;
    bool gesture(const properdesktop::ui::GestureEvent& event) override;
    void hoverMove(const properdesktop::ui::PointerEvent& event) override;
    void positionChanged() override;

private:
    class Canvas;

    struct TileKey
    {
        int page;
        // The scale in 1/4096ths of a pixel per point.
        int64_t scale;
        int column;
        int row;

        bool operator==(const TileKey&) const = default;
    };
    struct TileHash
    {
        size_t operator()(const TileKey& key) const;
    };
    struct Tile
    {
        TileKey key;
        double scale;
        properdesktop::ui::RasterImage image;
    };
    struct PendingTile
    {
        PdfRequest request;
        bool inSight;
    };

    // The page's top and left in the content at the zoom.
    double pageTop(int page, double zoom) const;
    double pageLeft(int page, double zoom) const;
    double contentWidth(double zoom) const;
    double contentHeight(double zoom) const;
    // The page at a height of the content, the nearest one in a gap.
    int pageAt(double y) const;
    // A point of the content as a page and a point on it.
    int pagePoint(properdesktop::ui::PointF content, properdesktop::ui::PointF* point) const;
    double ratio() const;
    double snap(double value) const;
    double minimumZoom() const;
    double maximumZoom() const { return std::max(fitZoom(), 8.0); }
    void setZoom(double zoom);
    void setPosition(properdesktop::ui::PointF position);
    void updateCurrentPage();
    void rest();
    // Asks for the tiles, text and links of what is in sight and near it.
    void updateTiles();
    void tileArrived(Tile tile);
    // Once every tile in sight is sharp, the others are of no more use.
    void dropOtherScales();
    void followLink(const properdesktop::pdf::Link& link);
    const properdesktop::pdf::Link* linkAt(properdesktop::ui::PointF content);
    void scrollToMatch();

    PdfDocument* m_document = nullptr;
    std::vector<properdesktop::base::Subscription> m_subscriptions;
    Canvas* m_canvas;
    // The sum of the heights before each page, in points.
    std::vector<double> m_heightsBefore;
    double m_widest = 0;
    double m_zoom = 1;
    double m_topInset = 0;
    bool m_pageShadows = true;
    bool m_fitted = true;
    properdesktop::ui::SizeF m_lastSize;
    double m_renderScale = 0;
    int m_currentPage = 0;
    // goToPage() says which page is current, whatever shows most.
    bool m_keepCurrentPage = false;

    // Most recently wanted first.
    std::list<Tile> m_tiles;
    std::unordered_map<TileKey, std::list<Tile>::iterator, TileHash> m_tileIndex;
    size_t m_tileBytes = 0;
    std::unordered_map<TileKey, PendingTile, TileHash> m_pendingTiles;
    std::unordered_map<int64_t, size_t> m_tilesAsked;
    properdesktop::base::Timer m_restTimer;

    bool m_anchored = false;
    properdesktop::ui::PointF m_anchorView;
    int m_anchorPage = 0;
    properdesktop::ui::PointF m_anchorPoint;
    properdesktop::ui::PointF m_pointer;
    double m_animationFrom = 1;
    double m_animationTo = 1;
    std::unique_ptr<properdesktop::ui::Animation> m_animation;
    double m_pinchScale = 1;

    TextPosition m_anchor;
    TextPosition m_focus;
    int m_matchPage = -1;
    int m_matchIndex = -1;
    PdfRequest m_resolve;
};
