// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "turnsaver.h"
#include "rotation.h"

#include <algorithm>

#include <sys/stat.h>

using namespace properdesktop;

TurnSaver::TurnSaver()
    : m_loop(base::EventLoop::current()->handle())
    , m_thread([this] { run(); })
{
}

TurnSaver::~TurnSaver()
{
    finish();
}

std::string TurnSaver::fileStamp(const std::string& path)
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0)
        return {};
    return std::to_string(st.st_ino) + ":" + std::to_string(st.st_size) + ":" + std::to_string(st.st_mtim.tv_sec) + "."
            + std::to_string(st.st_mtim.tv_nsec);
}

void TurnSaver::turn(const std::string& path, const std::string& mimeType, int quarterTurns)
{
    m_unwritten[path] += quarterTurns;
    std::lock_guard lock(m_mutex);
    const auto waiting = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const Job& job) { return job.path == path; });
    if (waiting != m_jobs.end())
        waiting->quarterTurns += quarterTurns;
    else
        m_jobs.push_back({path, mimeType, quarterTurns});
    m_wake.notify_one();
}

int TurnSaver::unwrittenTurns(const std::string& path) const
{
    const auto found = m_unwritten.find(path);
    return found == m_unwritten.end() ? 0 : found->second;
}

const std::string& TurnSaver::writtenStamp(const std::string& path) const
{
    static const std::string none;
    const auto found = m_stamps.find(path);
    return found == m_stamps.end() ? none : found->second;
}

void TurnSaver::finish()
{
    {
        std::unique_lock lock(m_mutex);
        m_stopping = true;
        m_wake.notify_one();
    }
    if (m_thread.joinable())
        m_thread.join();
}

void TurnSaver::run()
{
    std::unique_lock lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
        if (m_jobs.empty())
            return;
        const Job job = m_jobs.front();
        m_jobs.pop_front();
        lock.unlock();
        const int error = rotation::turn(job.path, job.mimeType, job.quarterTurns);
        m_loop.post([this, alive = std::weak_ptr<char>(m_alive), job, error] {
            if (!alive.expired())
                done(job, error);
        });
        lock.lock();
    }
}

void TurnSaver::done(const Job& job, int error)
{
    int& unwritten = m_unwritten[job.path];
    unwritten -= job.quarterTurns;
    m_stamps[job.path] = fileStamp(job.path);
    const bool last = unwritten == 0;
    if (last)
        m_unwritten.erase(job.path);
    if (last || error)
        written.emit(job.path, error);
}
