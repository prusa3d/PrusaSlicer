#pragma once 

#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

#include <jthread/JThread.hpp>

namespace Slic3r::Biz::AppInstance {



class AppInstanceMessageSenderMac: public AbstractAppInstanceMessageSender
{
public:
    AppInstanceMessageSenderMac() : AbstractAppInstanceMessageSender()
    {
        
    }

    /**
     * @brief Sends a message to all running instances of the application.
     * @param window_handle can be nullptr. (So multicast before app window is created is enabled.)
     */
    virtual void multicast_message(const std::string& message_type, const std::string& message_data, size_t instance_hash, void* window_handle) override;
    /**
     * @brief Send a message to the main instance of the application. Expects only one instance to be running.
     * @param window_handle can be nullptr. (So broadcast before app window is created is enabled.)
     */
    virtual void broadcast_message(const std::string& message_type, const std::string& message_data, size_t instance_hash, void* window_handle) override;

};

class AppInstanceMessageHandlerMac : public AbstractAppInstanceMessageHandler
{
public:
    AppInstanceMessageHandlerMac(Platform::IMainThreadDispatcher& dispatcher);
    ~AppInstanceMessageHandlerMac() override;

    /**
     * @brief Late initialization, called after Mainframe is created. 
     * Not needed in this implementation.
     */
    void init(void* window_handle) override {}

    /**
     * @brief Uses AbstractAppInstanceMessageSender to multicast a message. Adds instance hash.
     */
    void multicast_message(const std::string& message_type, const std::string& message_data) override
    {
        AppInstanceMessageSenderMac sender;
        sender.multicast_message(message_type, message_data, Platform::PlatformServices::instance().app_hash(), nullptr);
    }
    
    void on_becoming_primary_instance() override {}

private:
        void* m_impl_osx;
};

}