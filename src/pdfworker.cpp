// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "pdfworker.h"

#include "properdesktop/pdf/document.h"

using namespace properdesktop;

PdfWorker::PdfWorker()
    : m_thread([this] { threadMain(); })
{
}

PdfWorker::~PdfWorker()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
        for (std::deque<Task>& queue : m_queues) {
            for (Task& task : queue)
                task.cancelled->store(true);
            queue.clear();
        }
    }
    m_wake.notify_one();
    m_thread.join();
}

PdfRequest PdfWorker::open(const std::string& path, const std::string& password,
                           std::function<void(int status)> done)
{
    auto shared = std::make_shared<std::function<void(int)>>(std::move(done));
    base::LoopHandle loop = base::EventLoop::current()->handle();
    return enqueue(Urgency::Now, [path, password, shared, loop](std::unique_ptr<pdf::Document>& document,
                                                                 const std::shared_ptr<std::atomic<bool>>& cancelled) {
        pdf::Document::Opening opening = pdf::Document::open(path, password);
        document = std::move(opening.document);
        const int status = int(opening.status);
        loop.post([shared, status, cancelled] {
            if (!cancelled->exchange(true))
                (*shared)(status);
        });
    });
}

PdfRequest PdfWorker::enqueue(Urgency urgency,
                              std::function<void(std::unique_ptr<pdf::Document>&,
                                                 const std::shared_ptr<std::atomic<bool>>&)> run)
{
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(m_mutex);
        m_queues[int(urgency)].push_back(
                {cancelled, [run = std::move(run), cancelled](std::unique_ptr<pdf::Document>& document) {
                     run(document, cancelled);
                 }});
    }
    m_wake.notify_one();
    return PdfRequest(cancelled);
}

void PdfWorker::threadMain()
{
    std::unique_ptr<pdf::Document> document;
    std::unique_lock lock(m_mutex);
    while (true) {
        Task task;
        for (std::deque<Task>& queue : m_queues) {
            while (!queue.empty() && queue.front().cancelled->load())
                queue.pop_front();
            if (!queue.empty()) {
                task = std::move(queue.front());
                queue.pop_front();
                break;
            }
        }
        if (!task.run) {
            if (m_stopping)
                break;
            m_wake.wait(lock);
            continue;
        }
        lock.unlock();
        task.run(document);
        lock.lock();
    }
}
