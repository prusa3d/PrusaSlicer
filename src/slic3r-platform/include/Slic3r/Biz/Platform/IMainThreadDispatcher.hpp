#pragma once

#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Platform/FunctionUtils.hpp"

namespace Slic3r::Biz::Platform {
class IMainThreadDispatcher
{
public:
    using Function = MoveOnlyFunction;
    virtual ~IMainThreadDispatcher() = default;

    virtual bool dispatch_on_main_thread(Function&& func) = 0;
    virtual bool dispatch_on_main_thread_after(Function&& func) = 0;
    virtual bool dispatch_enqueued() = 0;
    [[nodiscard]] virtual bool is_closed() const = 0;

    /* Should dispatch all events and prevent any more events from entering the queue. */
    virtual void close() = 0;
};

}
