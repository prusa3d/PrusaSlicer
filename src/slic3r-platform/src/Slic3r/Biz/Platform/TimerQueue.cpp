#include "Slic3r/Biz/Platform/TimerQueue.hpp"

#include "Slic3r/Assert.hpp"

#include <algorithm>


namespace Slic3r::Biz::Platform {


TimerQueue::TimerQueue(IMainThreadDispatcher& thread_dispatcher)
    : m_main_thread_dispatcher(thread_dispatcher)
{
    m_thread = JThread::JThread([this](JThread::StopToken stop_token) {
        Clock::duration sleep_duration = std::chrono::nanoseconds(1);
        while (true) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, sleep_duration);
            if (stop_token.stop_requested())
                return;
            auto now = Clock::now();
            while (!m_timers.empty() && m_timers.front().time <= now) {
                // If the timer does not dispatch because the dispatcher is locked, just ignore it.
                [[maybe_unused]] const bool dispatched{
                    m_main_thread_dispatcher.dispatch_on_main_thread(m_timers.front().callback)
                };

                if (m_timers.front().repeat_ms) {
                    m_timers.front().time = now + *m_timers.front().repeat_ms;
                    std::sort(m_timers.begin(), m_timers.end());
                }
                else
                    m_timers.erase(m_timers.begin());
            }
            sleep_duration = m_timers.empty()
                ? std::chrono::minutes(10)
                : m_timers.front().time - now + std::chrono::milliseconds(1);
        }
    });
}



TimerQueue::~TimerQueue()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timers = {};
    }
	m_thread.request_stop();
    m_cv.notify_all();
    // jthread will be join in its destructor.
}



TimerQueue::TimerID TimerQueue::set_timer(std::chrono::milliseconds duration_ms, Callback callback, bool repeat)
{
    TimerID id(m_next_timer_id++);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timers.push_back(TimerEvent(id, duration_ms, std::move(callback), repeat));
        std::sort(m_timers.begin(), m_timers.end());
    }
    m_cv.notify_all();
    return id;
}


void TimerQueue::cancel_timer(TimerID id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = std::find_if(m_timers.begin(), m_timers.end(),
        [id](const TimerEvent& event) { return event.id == id; });
    ASSERT(it != m_timers.end());
    m_timers.erase(it);
}



bool TimerQueue::is_timer_running(TimerID id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::find_if(m_timers.begin(), m_timers.end(),
        [id](const TimerEvent& event) { return event.id == id; }) != m_timers.end();
}



TimerQueue::TimerEvent::TimerEvent(TimerID id_, std::chrono::milliseconds duration_ms, Callback callback, bool repeat)
: time(Clock::now() + duration_ms),
  callback(std::move(callback)),
  repeat_ms{ std::nullopt },
  id{id_}

{
    if (repeat)
        repeat_ms = duration_ms;
}

} // namespace Slic3r::Biz::Platform
