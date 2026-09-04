#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "AppInstanceMessageHandlerLinux.hpp"

namespace Slic3r::Biz::AppInstance
{
std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageSender> create_app_instance_message_sender()
{
    return std::make_unique<AppInstanceMessageSenderLinux>();
}
std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageHandler> create_app_instance_message_handler(Platform::IMainThreadDispatcher& dispatcher)
{
    return std::make_unique<AppInstanceMessageHandlerLinux>(dispatcher);
}

}