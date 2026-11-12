#pragma once

#include <Slic3r/Biz/Platform/IMainThreadDispatcher.hpp>

#include <vector>
#include <mutex>
#include <atomic>

namespace Slic3r::App::Platform {

class StdMainThreadDispatcher : public Biz::Platform::IMainThreadDispatcher
{
public:
    bool dispatch_on_main_thread(Function&& func) override;
    bool dispatch_on_main_thread_after(Function&& func) override;
    bool dispatch_enqueued() override;
    [[nodiscard]] bool is_closed() const override;
    void close() override;

protected:
    virtual void on_queue_insert() {}
private:
    using Functions = std::vector<Function>;
    static bool process_queue(Functions& queue, std::mutex& queue_mutex);
private:
    mutable std::mutex m_queue_mutex;
    mutable std::mutex m_call_after_queue_mutex;

    Functions m_queue;
    Functions m_call_after_queue;
    std::atomic<bool> m_closed;
};

}
