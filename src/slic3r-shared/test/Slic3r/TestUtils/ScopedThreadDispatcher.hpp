#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

namespace Tests {

class ScopedThreadDispatcher
{
public:
    explicit ScopedThreadDispatcher(Slic3r::Biz::Platform::IMainThreadDispatcher& dispatcher) :
        m_dispatcher(dispatcher)
    {}

    ~ScopedThreadDispatcher()
    {
        m_dispatcher.close();
    }

    ScopedThreadDispatcher(const ScopedThreadDispatcher&)            = delete;
    ScopedThreadDispatcher& operator=(const ScopedThreadDispatcher&) = delete;

private:
    Slic3r::Biz::Platform::IMainThreadDispatcher& m_dispatcher;
};

} // namespace Tests
