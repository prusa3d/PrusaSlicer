#pragma once

#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"

#include <memory>

namespace Slic3r::Biz::AppInstance {

std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageSender>
create_app_instance_message_sender();
std::unique_ptr<Biz::AppInstance::AbstractAppInstanceMessageHandler>
create_app_instance_message_handler(Platform::IMainThreadDispatcher& dispatcher);

} // namespace Slic3r::Biz::AppInstance
