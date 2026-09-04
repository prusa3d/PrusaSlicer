#pragma once

#include "Slic3r/Biz/Platform/IAppInstanceMessageContentListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

namespace Slic3r::Biz::Platform {

class IAppInstanceMessageHandler : public WithListeners<IAppInstanceMessageContentListener>
{
public:

    virtual ~IAppInstanceMessageHandler() = default;

    /**
     * @brief Late initialization, called after Mainframe is created.
     */
    virtual void init(void* window_handle) = 0;

    /**
     * @brief Parses message and calls IAppInstanceMessageContentListener methods.
     * On linux, this is called from the worker thread!
     */
    virtual void handle_message(const std::string& message) = 0;

    /**
     * @brief Uses AbstractAppInstanceMessageSender to multicast a message. Adds instance hash.
     */
    virtual void
    multicast_message(const std::string& message_type, const std::string& message_data) = 0;

    /**
     * @brief Called handle_message_type_other_closed gets false from anther instance running. Linux does reregister dbus listener on this.
     */
    virtual void on_becoming_primary_instance() = 0;
};

} // namespace Slic3r::Biz::Platform
