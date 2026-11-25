// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "base/event_loop.h"
#include "base/signal.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// The process's writes of turned images: on a thread of its own, one file
// at a time, in the order asked; turns asked for a file still waiting join
// its write. Results come back on the loop that made it.
class TurnSaver
{
public:
    TurnSaver();
    // Waits for every write asked for.
    ~TurnSaver();

    TurnSaver(const TurnSaver&) = delete;
    TurnSaver& operator=(const TurnSaver&) = delete;

    // Quarters clockwise, negative for left.
    void turn(const std::string& path, const std::string& mimeType, int quarterTurns);
    // Turns asked for the file and not written yet, waiting or being written.
    int unwrittenTurns(const std::string& path) const;
    bool isIdle() const { return m_unwritten.empty(); }
    // The file as our last write of it left it; empty when there was none.
    const std::string& writtenStamp(const std::string& path) const;
    // Blocks until every write asked for is done; for quitting.
    void finish();

    // A file's turns are all written, or one of the writes failed with
    // this rotation::turn() result.
    properdesktop::base::Signal<const std::string&, int> written;

    // What tells one write of a file from another: inode, size and
    // modification time.
    static std::string fileStamp(const std::string& path);

private:
    struct Job
    {
        std::string path;
        std::string mimeType;
        int quarterTurns = 0;
    };

    void run();
    void done(const Job& job, int error);

    properdesktop::base::LoopHandle m_loop;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<Job> m_jobs;
    bool m_stopping = false;
    // The loop's side.
    std::map<std::string, int> m_unwritten;
    std::map<std::string, std::string> m_stamps;
    std::shared_ptr<char> m_alive = std::make_shared<char>();
    // Last: it starts once everything it uses is made.
    std::thread m_thread;
};
