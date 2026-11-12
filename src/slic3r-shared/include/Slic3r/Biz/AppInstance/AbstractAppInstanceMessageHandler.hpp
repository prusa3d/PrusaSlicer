#pragma once

#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include <Slic3r/Biz/Platform/IAppInstanceMessageHandler.hpp>

#include <string>
#include <map>
#include <functional>

namespace Slic3r::Biz::AppInstance {

class AbstractAppInstanceMessageSender
{
public:
    AbstractAppInstanceMessageSender()          = default;
    virtual ~AbstractAppInstanceMessageSender() = default;

    /**
     * @brief Sends a message to all running instances of the application.
     * @param window_handle can be nullptr. (So multicast before app window is created is enabled.)
     */
    virtual void multicast_message(
        const std::string& message_type,
        const std::string& message_data,
        size_t instance_hash,
        void* window_handle
    ) = 0;
    /**
     * @brief Send a message to the main instance of the application. Expects only one instance to be running.
     * @param window_handle can be nullptr. (So broadcast before app window is created is enabled.)
     */
    virtual void broadcast_message(
        const std::string& message_type,
        const std::string& message_data,
        size_t instance_hash,
        void* window_handle
    ) = 0;
};

class AbstractAppInstanceMessageHandler : public Platform::IAppInstanceMessageHandler
{
public:
    AbstractAppInstanceMessageHandler(Platform::IMainThreadDispatcher& dispatcher);
    virtual ~AbstractAppInstanceMessageHandler();

    void handle_message(const std::string& message) override;

protected:
    /**
     * @brief Called when app should go to front.
     * On linux, this is called from the worker thread!
     */
    void dispatch_go_to_front();

private:
    using MessageHandlerFn = std::function<void(const std::string&)>;

    mutable std::mutex
        m_dispatcher_mutex; // There are 2 worker threads on linux, so we need to protect the dispatcher call.
    Platform::IMainThreadDispatcher& m_dispatcher;
    std::map<std::string, MessageHandlerFn> m_message_handlers;

    /**
     * @brief Handles the message type "CLI" and calls the appropriate listener method.
     * On linux, this is called from the worker thread!
     */
    void handle_message_type_cli(const std::string& data);

    /**
     * @brief Handles the message type "LOGIN" - a login callback forwarded from another
     * instance. Only dispatches the login data to listeners; it never re-forwards, which
     * is what prevents an infinite multicast loop between instances (see handle_message_type_cli).
     * On linux, this is called from the worker thread!
     */
    void handle_message_type_login(const std::string& data);

    /**
     * @brief Dispatches a single prusaslicer://login url to the listeners on the main thread.
     */
    void dispatch_login(const std::string& url);

    /**
     * @brief Handles the message type "STORE_READ" and calls the appropriate listener method.
     * On linux, this is called from the worker thread!
     */
    void handle_message_type_store_read(const std::string& data);

    /**
     * @brief Handles the message type "OTHER_CLOSED" by trying to acquire lockfile (on mac only).
     * On linux, this is called from the worker thread!
     */
    void handle_message_type_other_closed(const std::string& data);

    void handle_message_type_request_backup_id(const std::string& data);

    void handle_message_type_backup_id(const std::string& data);
};
} // namespace Slic3r::Biz::AppInstance
