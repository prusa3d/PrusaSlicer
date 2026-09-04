#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <optional>

#include <jthread/JThread.hpp>

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"


namespace Slic3r::Biz::Platform {

class TimerQueue
{
private:
    using Callback = std::function<void()>;

public:
    struct TimerID {
        explicit TimerID(int id) : id(id) {}
        TimerID() : id(-1) {}
        bool operator==(const TimerID& other) const { return id == other.id; }
        int id;
    };

    TimerQueue(IMainThreadDispatcher& thread_dispatcher);
    ~TimerQueue();

    TimerID set_timer(std::chrono::milliseconds duration_ms, Callback callback, bool repeat = false);
    void cancel_timer(TimerID id);
    bool is_timer_running(TimerID id) const;

private:
    using Clock = std::chrono::steady_clock;

    struct TimerEvent {
        TimerEvent(TimerID id, std::chrono::milliseconds duration_ms, Callback callback, bool repeat);
        bool operator<(const TimerEvent& other) const { return time < other.time; }

        std::chrono::time_point<Clock> time;
        Callback callback;
        std::optional<std::chrono::milliseconds> repeat_ms;
        TimerID id;
    };
    int m_next_timer_id{0};


    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<TimerEvent> m_timers;
    JThread::JThread m_thread;

    IMainThreadDispatcher& m_main_thread_dispatcher;
};

} // namespace Slic3r::Biz::Platform
