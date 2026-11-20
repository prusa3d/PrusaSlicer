#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Runs best-effort requests (revocations) one after another on its own
 * thread. Destruction drops what has not started yet and joins; the running request is bounded
 * by its timeouts.
 */
class BackgroundWorker
{
public:
    BackgroundWorker() : m_thread([this]() { run(); }) {}

    BackgroundWorker(const BackgroundWorker&)            = delete;
    BackgroundWorker& operator=(const BackgroundWorker&) = delete;

    ~BackgroundWorker()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
            m_tasks.clear();
        }
        m_condition.notify_all();
        m_thread.join();
    }

    void add(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) {
                return;
            }
            m_tasks.push_back(std::move(task));
        }
        m_condition.notify_all();
    }

private:
    void run()
    {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this]() { return m_stopping || !m_tasks.empty(); });
                if (m_tasks.empty()) {
                    return;
                }
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
            task();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<std::function<void()>> m_tasks;
    bool m_stopping{false};
    std::thread m_thread;
};

} // namespace Slic3r::Biz::UserAccount
