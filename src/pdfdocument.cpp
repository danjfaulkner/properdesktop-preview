// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "pdfdocument.h"

#include "base/mime_database.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace properdesktop;
using Urgency = PdfWorker::Urgency;

static constexpr size_t kPagesKept = 64;
static constexpr size_t kThumbnailBytes = size_t(64) * 1024 * 1024;

PdfDocument::PdfDocument(std::string path)
    : m_path(std::move(path))
    , m_worker(std::make_unique<PdfWorker>())
    , m_pages(kPagesKept)
    , m_thumbnails(kThumbnailBytes)
{
    m_opening = m_worker->open(m_path, {}, [this](int status) { opened(status); });
}

PdfDocument::~PdfDocument() = default;

bool PdfDocument::isPdf(const std::string& path)
{
    return base::mime::typeForFileName(std::filesystem::path(path).filename().string()) == "application/pdf";
}

void PdfDocument::unlock(const std::string& password)
{
    m_state = State::Opening;
    m_opening = m_worker->open(m_path, password, [this](int status) { opened(status); });
}

void PdfDocument::opened(int status)
{
    switch (pdf::Document::Status(status)) {
    case pdf::Document::Status::PasswordRequired:
        m_state = State::Locked;
        break;
    case pdf::Document::Status::PasswordWrong:
        m_state = State::WrongPassword;
        break;
    case pdf::Document::Status::Unreadable:
        m_state = State::Unreadable;
        break;
    case pdf::Document::Status::Opened:
        m_info = m_worker->run(
                Urgency::Now,
                [](const pdf::Document* document, const std::atomic<bool>*) {
                    std::vector<ui::SizeF> sizes;
                    for (const base::SizeF& size : document->pageSizes())
                        sizes.push_back({size.width(), size.height()});
                    return sizes;
                },
                [this](std::vector<ui::SizeF> sizes) {
                    m_sizes = std::move(sizes);
                    m_state = m_sizes.empty() ? State::Unreadable : State::Ready;
                    stateChanged();
                    if (m_state != State::Ready)
                        return;
                    // Behind the first pages, which are asked for as the state changes.
                    m_outlineRequest = m_worker->run(
                            Urgency::Background,
                            [](const pdf::Document* document, const std::atomic<bool>*) { return document->outline(); },
                            [this](std::vector<pdf::OutlineItem> outline) {
                                m_outline = std::move(outline);
                                m_outlineRead = true;
                                outlineChanged();
                            });
                });
        return;
    }
    stateChanged();
}

PdfDocument::PageData& PdfDocument::data(int page)
{
    if (PageData* found = m_pages.find(page))
        return *found;
    m_pages.insert(page, std::make_unique<PageData>());
    return *m_pages.find(page);
}

const pdf::PageText* PdfDocument::text(int page, Urgency urgency)
{
    if (m_state != State::Ready || page < 0 || page >= pageCount())
        return nullptr;
    PageData& entry = data(page);
    if (entry.text)
        return &*entry.text;
    PdfRequest& request = m_textRequests[page];
    if (!request.isPending()) {
        request = m_worker->run(
                urgency, [page](const pdf::Document* document, const std::atomic<bool>*) { return document->text(page); },
                [this, page](pdf::PageText text) {
                    m_textRequests.erase(page);
                    data(page).text = std::move(text);
                    pageDataChanged(page);
                });
    }
    return nullptr;
}

const std::vector<pdf::Link>* PdfDocument::links(int page, Urgency urgency)
{
    if (m_state != State::Ready || page < 0 || page >= pageCount())
        return nullptr;
    PageData& entry = data(page);
    if (entry.links)
        return &*entry.links;
    PdfRequest& request = m_linkRequests[page];
    if (!request.isPending()) {
        request = m_worker->run(
                urgency, [page](const pdf::Document* document, const std::atomic<bool>*) { return document->links(page); },
                [this, page](std::vector<pdf::Link> links) {
                    m_linkRequests.erase(page);
                    data(page).links = std::move(links);
                    pageDataChanged(page);
                });
    }
    return nullptr;
}

void PdfDocument::keepPageRequests(int first, int last)
{
    const auto outside = [first, last](const auto& entry) { return entry.first < first || entry.first > last; };
    std::erase_if(m_textRequests, outside);
    std::erase_if(m_linkRequests, outside);
}

const ui::RasterImage* PdfDocument::thumbnail(int page, base::Size box)
{
    if (m_state != State::Ready || page < 0 || page >= pageCount() || box.isEmpty())
        return nullptr;
    const Thumbnail* entry = m_thumbnails.find(page);
    if (entry && entry->box == box)
        return &entry->image;
    const auto pending = m_thumbnailRequests.find(page);
    if (pending != m_thumbnailRequests.end() && pending->second.box == box && pending->second.request.isPending())
        return nullptr;
    const ui::SizeF size = m_sizes[size_t(page)];
    const double scale = std::min(box.width() / size.width, box.height() / size.height);
    const base::Rect region(0, 0, std::max(1, int(std::round(size.width * scale))),
                            std::max(1, int(std::round(size.height * scale))));
    PendingThumbnail& request = m_thumbnailRequests[page];
    request.box = box;
    request.request = m_worker->run(
            Urgency::Background,
            [page, scale, region](const pdf::Document* document, const std::atomic<bool>* cancelled) {
                pdf::Bitmap bitmap = document->render(page, scale, region, cancelled);
                return ui::RasterImage(bitmap.width, bitmap.height, std::move(bitmap.pixels), true);
            },
            [this, page, box](ui::RasterImage image) {
                m_thumbnailRequests.erase(page);
                if (image.isNull())
                    return;
                const size_t bytes = image.sizeInBytes();
                m_thumbnails.insert(page, std::make_unique<Thumbnail>(Thumbnail {box, std::move(image)}), bytes);
                thumbnailChanged(page);
            });
    return nullptr;
}

const ui::RasterImage* PdfDocument::anyThumbnail(int page)
{
    const Thumbnail* entry = m_thumbnails.find(page);
    return entry ? &entry->image : nullptr;
}

void PdfDocument::keepThumbnailRequests(int first, int last)
{
    std::erase_if(m_thumbnailRequests,
                  [first, last](const auto& entry) { return entry.first < first || entry.first > last; });
}

PdfRequest PdfDocument::resolve(const pdf::Destination& destination, std::function<void(int page, ui::PointF point)> done)
{
    if (m_state != State::Ready || destination.page < 0 || destination.page >= pageCount())
        return {};
    return m_worker->run(
            Urgency::Now,
            [destination](const pdf::Document* document, const std::atomic<bool>*) {
                return document->position(destination);
            },
            [page = destination.page, done = std::move(done)](base::PointF point) {
                done(page, {point.x(), point.y()});
            });
}

void PdfDocument::readText(TextPosition from, TextPosition to, std::function<void(std::string text)> done)
{
    if (m_state != State::Ready || to <= from) {
        m_textRead.cancel();
        return;
    }
    to.page = std::min(to.page, pageCount() - 1);
    readTextPage(std::make_shared<TextRead>(TextRead {from, to, from.page, {}, std::move(done)}));
}

void PdfDocument::readTextPage(std::shared_ptr<TextRead> read)
{
    const int page = read->page;
    m_textRead = m_worker->run(
            Urgency::Near,
            [page, from = read->from, to = read->to](const pdf::Document* document, const std::atomic<bool>*) {
                const pdf::PageText pageText = document->text(page);
                const size_t size = pageText.characters.size();
                const size_t first = page == from.page ? std::min(from.index, size) : 0;
                const size_t last = page == to.page ? std::min(to.index, size) : size;
                return pageText.text(first, last - std::min(first, last));
            },
            [this, read](std::string part) {
                if (!read->text.empty() && read->page != read->from.page)
                    read->text += '\n';
                read->text += part;
                if (++read->page > read->to.page)
                    read->done(std::move(read->text));
                else
                    readTextPage(read);
            });
}

void PdfDocument::setSearch(const std::string& query, int firstPage)
{
    m_search.cancel();
    m_query = query;
    m_matches.clear();
    m_matchCount = 0;
    m_searched = 0;
    m_searchStart = std::clamp(firstPage, 0, std::max(0, pageCount() - 1));
    if (m_query.empty() || m_state != State::Ready)
        m_searched = pageCount();
    else
        searchNext();
    searchChanged();
}

const std::vector<pdf::Match>* PdfDocument::matches(int page) const
{
    const auto found = m_matches.find(page);
    return found == m_matches.end() ? nullptr : &found->second;
}

void PdfDocument::searchNext()
{
    if (m_searched >= pageCount())
        return;
    const int page = (m_searchStart + m_searched) % pageCount();
    m_search = m_worker->run(
            Urgency::Background,
            [page, query = m_query](const pdf::Document* document, const std::atomic<bool>*) {
                return document->find(page, query);
            },
            [this, page](std::vector<pdf::Match> found) {
                m_matchCount += int(found.size());
                if (!found.empty())
                    m_matches[page] = std::move(found);
                ++m_searched;
                searchNext();
                searchChanged();
            });
}
