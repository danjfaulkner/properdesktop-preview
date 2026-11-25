// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/event_loop.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace properdesktop::pdf {
class Document;
}

// A request handed to a PdfWorker. Cancelled, it is dropped before it runs,
// a render in progress stops between its parts, and its result is not
// delivered. Cancelled as well when it goes.
class PdfRequest
{
public:
    PdfRequest() = default;
    explicit PdfRequest(std::shared_ptr<std::atomic<bool>> cancelled)
        : m_cancelled(std::move(cancelled))
    {
    }
    ~PdfRequest() { cancel(); }

    PdfRequest(PdfRequest&&) = default;
    PdfRequest& operator=(PdfRequest&& other)
    {
        if (this != &other) {
            cancel();
            m_cancelled = std::move(other.m_cancelled);
        }
        return *this;
    }

    void cancel()
    {
        if (m_cancelled)
            m_cancelled->store(true);
    }
    // Neither delivered nor cancelled yet.
    bool isPending() const { return m_cancelled && !m_cancelled->load(); }

private:
    std::shared_ptr<std::atomic<bool>> m_cancelled;
};

// One document's PDFium calls, on a thread of their own, the most urgent
// first and in the order asked within one urgency. Results come back to the
// loop of the thread that asked.
class PdfWorker
{
public:
    enum class Urgency {
        // Opening, what the pointer is on.
        Now,
        // What is in sight.
        Visible,
        // What is about to be.
        Near,
        // Thumbnails, the outline, searching.
        Background,
    };

    PdfWorker();
    // Drops what is queued and waits for the call in progress, cancelled.
    ~PdfWorker();

    PdfWorker(const PdfWorker&) = delete;
    PdfWorker& operator=(const PdfWorker&) = delete;

    // work(document, cancelled) on the worker, where `document` is null until
    // an open() has succeeded, then done(result) on the loop.
    template<typename Work, typename Done>
    PdfRequest run(Urgency urgency, Work work, Done done);

    // Opens the file on the worker, in place of the document there.
    // done(status as an int of pdf::Document::Status).
    PdfRequest open(const std::string& path, const std::string& password, std::function<void(int status)> done);

private:
    struct Task
    {
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::function<void(std::unique_ptr<properdesktop::pdf::Document>& document)> run;
    };

    PdfRequest enqueue(Urgency urgency, std::function<void(std::unique_ptr<properdesktop::pdf::Document>&,
                                                           const std::shared_ptr<std::atomic<bool>>&)> run);
    void threadMain();

    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Task> m_queues[4];
    bool m_stopping = false;
    std::thread m_thread;
};

template<typename Work, typename Done>
PdfRequest PdfWorker::run(Urgency urgency, Work work, Done done)
{
    // std::function copies; the callables and the result may only move.
    auto shared = std::make_shared<std::pair<Work, Done>>(std::move(work), std::move(done));
    properdesktop::base::LoopHandle loop = properdesktop::base::EventLoop::current()->handle();
    return enqueue(urgency, [shared, loop](std::unique_ptr<properdesktop::pdf::Document>& document,
                                           const std::shared_ptr<std::atomic<bool>>& cancelled) {
        using Result = std::invoke_result_t<Work&, const properdesktop::pdf::Document*, const std::atomic<bool>*>;
        auto result = std::make_shared<Result>(shared->first(document.get(), cancelled.get()));
        if (cancelled->load())
            return;
        loop.post([shared, result, cancelled] {
            if (cancelled->exchange(true))
                return;
            shared->second(std::move(*result));
        });
    });
}
