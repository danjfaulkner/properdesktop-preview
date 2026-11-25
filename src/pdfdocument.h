// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "pdfworker.h"

#include "properdesktop/pdf/document.h"

#include "base/geometry.h"
#include "base/lru_cache.h"
#include "base/signal.h"
#include "ui/geometry.h"
#include "ui/raster_image.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// A place in the text of the document: before character `index` of `page`.
struct TextPosition
{
    int page = 0;
    size_t index = 0;

    auto operator<=>(const TextPosition&) const = default;
};

// A PDF as the UI sees it, everything read on its worker: the pages' sizes
// and the outline once, each page's text and links as they are asked for,
// thumbnails, and a search through every page. Asking for something not
// there yet asks the worker and tells by a signal when it arrives.
class PdfDocument
{
public:
    enum class State {
        Opening,
        // The file wants its password, or the one given was wrong.
        Locked,
        WrongPassword,
        Unreadable,
        Ready,
    };

    explicit PdfDocument(std::string path);
    ~PdfDocument();

    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    // By its name.
    static bool isPdf(const std::string& path);

    const std::string& path() const { return m_path; }
    State state() const { return m_state; }
    void unlock(const std::string& password);

    int pageCount() const { return int(m_sizes.size()); }
    // In points, as shown.
    const std::vector<properdesktop::ui::SizeF>& pageSizes() const { return m_sizes; }
    // Empty until read, after the first pages are drawn.
    const std::vector<properdesktop::pdf::OutlineItem>& outline() const { return m_outline; }
    bool outlineRead() const { return m_outlineRead; }

    // Null until it arrives.
    const properdesktop::pdf::PageText* text(int page, PdfWorker::Urgency urgency = PdfWorker::Urgency::Near);
    const std::vector<properdesktop::pdf::Link>* links(int page, PdfWorker::Urgency urgency = PdfWorker::Urgency::Near);
    // Text and links asked for pages outside these are not read after all.
    void keepPageRequests(int first, int last);
    // The page fitted into `box` device pixels; another box asks again.
    const properdesktop::ui::RasterImage* thumbnail(int page, properdesktop::base::Size box);
    // The page's thumbnail in whatever box it came, as a stand-in.
    const properdesktop::ui::RasterImage* anyThumbnail(int page);
    // Thumbnails asked for pages outside these are not drawn after all.
    void keepThumbnailRequests(int first, int last);

    // The point of the page as shown it goes to. The caller keeps the request.
    [[nodiscard]] PdfRequest resolve(const properdesktop::pdf::Destination& destination,
                 std::function<void(int page, properdesktop::ui::PointF point)> done);
    // UTF-8 from `from` up to `to`, lines of separate pages apart, read a
    // page at a time so what is in sight is drawn in between. A newer read,
    // or the document going, drops it.
    void readText(TextPosition from, TextPosition to, std::function<void(std::string text)> done);

    // Every page searched, from `firstPage` on and round, without case.
    void setSearch(const std::string& query, int firstPage);
    const std::string& searchText() const { return m_query; }
    // Matches of the pages searched so far.
    const std::vector<properdesktop::pdf::Match>* matches(int page) const;
    int matchCount() const { return m_matchCount; }
    bool searchFinished() const { return m_searched >= pageCount(); }

    PdfWorker& worker() { return *m_worker; }

    properdesktop::base::Signal<> stateChanged;
    properdesktop::base::Signal<> outlineChanged;
    // Its text or links arrived.
    properdesktop::base::Signal<int> pageDataChanged;
    properdesktop::base::Signal<int> thumbnailChanged;
    // More pages were searched.
    properdesktop::base::Signal<> searchChanged;

private:
    struct PageData
    {
        std::optional<properdesktop::pdf::PageText> text;
        std::optional<std::vector<properdesktop::pdf::Link>> links;
    };
    struct Thumbnail
    {
        properdesktop::base::Size box;
        properdesktop::ui::RasterImage image;
    };
    struct PendingThumbnail
    {
        properdesktop::base::Size box;
        PdfRequest request;
    };

    struct TextRead
    {
        TextPosition from;
        TextPosition to;
        int page;
        std::string text;
        std::function<void(std::string text)> done;
    };

    void opened(int status);
    void readTextPage(std::shared_ptr<TextRead> read);
    void searchNext();
    PageData& data(int page);

    std::string m_path;
    std::unique_ptr<PdfWorker> m_worker;
    State m_state = State::Opening;
    std::vector<properdesktop::ui::SizeF> m_sizes;
    std::vector<properdesktop::pdf::OutlineItem> m_outline;
    bool m_outlineRead = false;
    PdfRequest m_opening;
    PdfRequest m_info;
    PdfRequest m_outlineRequest;
    PdfRequest m_textRead;
    // The pages near what was asked for last; far ones are read again.
    properdesktop::base::LruCache<int, PageData> m_pages;
    std::unordered_map<int, PdfRequest> m_textRequests;
    std::unordered_map<int, PdfRequest> m_linkRequests;
    properdesktop::base::LruCache<int, Thumbnail> m_thumbnails;
    std::unordered_map<int, PendingThumbnail> m_thumbnailRequests;

    std::string m_query;
    int m_searchStart = 0;
    int m_searched = 0;
    int m_matchCount = 0;
    std::unordered_map<int, std::vector<properdesktop::pdf::Match>> m_matches;
    PdfRequest m_search;
};
