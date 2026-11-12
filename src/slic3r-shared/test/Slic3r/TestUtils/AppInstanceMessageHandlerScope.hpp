#pragma once

#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

namespace Tests {

class AppInstanceMessageHandlerScope
{
public:
    explicit AppInstanceMessageHandlerScope(Slic3r::Biz::Platform::IMainThreadDispatcher& dispatcher) :
        m_dispatcher(dispatcher)
    {
        Slic3r::Biz::Platform::PlatformServices::instance().set_app_instance_message_handler(
            Slic3r::Biz::AppInstance::create_app_instance_message_handler(m_dispatcher)
        );
    }

    ~AppInstanceMessageHandlerScope()
    {
        m_dispatcher.close();
        Slic3r::Biz::Platform::PlatformServices::instance().set_app_instance_message_handler(nullptr);
    }

    AppInstanceMessageHandlerScope(const AppInstanceMessageHandlerScope&)            = delete;
    AppInstanceMessageHandlerScope& operator=(const AppInstanceMessageHandlerScope&) = delete;

private:
    Slic3r::Biz::Platform::IMainThreadDispatcher& m_dispatcher;
};

} // namespace Tests
