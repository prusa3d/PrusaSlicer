#include "Slic3r/Biz/AppInstance/AppInstanceMessageHandlerFactory.hpp"
#include "AppInstanceMessageHandlerMac.hpp"

namespace Slic3r::Biz::AppInstance
{
std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageSender> create_app_instance_message_sender()
{
    return std::make_unique<AppInstanceMessageSenderMac>();
}
std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageHandler> create_app_instance_message_handler(Platform::IMainThreadDispatcher& dispatcher)
{
    return std::make_unique<AppInstanceMessageHandlerMac>(dispatcher);
}

}