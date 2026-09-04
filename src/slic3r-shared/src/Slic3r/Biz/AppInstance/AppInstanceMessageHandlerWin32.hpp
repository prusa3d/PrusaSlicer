#pragma once 

#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"

#include <Windows.h>

namespace Slic3r::Biz::AppInstance {

class AppInstanceMessageSenderWin32: public AbstractAppInstanceMessageSender
{
public:
	AppInstanceMessageSenderWin32() = default;

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

    BOOL enum_windows_process_multicast(_In_ HWND hwnd);
    BOOL enum_windows_process_broadcast(_In_ HWND hwnd);

private:
   // Temporary data stored as members when enum_windows_process_multicast / enum_windows_process_broadcast is ongoing.
   std::wstring m_multicast_message;
   HWND         m_window_handle {nullptr};
   size_t       m_instance_hash {0};
};

class AppInstanceMessageHandlerWin32 : public AbstractAppInstanceMessageHandler
{
public:
	AppInstanceMessageHandlerWin32(Platform::IMainThreadDispatcher& dispatcher);
    ~AppInstanceMessageHandlerWin32() override;

    /**
     * @brief Late initialization, called after Mainframe is created. 
     */
	void init(void* window_handle) override;

    /**
     * @brief Uses AbstractAppInstanceMessageSender to multicast a message. Adds instance hash.
     */
    void multicast_message(const std::string& message_type, const std::string& message_data) override;

    void on_becoming_primary_instance() override {}

private:
    HWND m_window_handle {nullptr};
    bool m_init {false};
};

}