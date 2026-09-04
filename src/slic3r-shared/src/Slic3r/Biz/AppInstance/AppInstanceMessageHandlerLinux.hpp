#pragma once 

#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"

#include <jthread/JThread.hpp>
#include <condition_variable>
#include <dbus/dbus.h>

namespace Slic3r::Biz::AppInstance {

class AppInstanceMessageSenderLinux: public AbstractAppInstanceMessageSender
{
public:
    AppInstanceMessageSenderLinux() = default;

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

class AppInstanceMessageHandlerLinux : public AbstractAppInstanceMessageHandler
{
public:
    AppInstanceMessageHandlerLinux(Platform::IMainThreadDispatcher& dispatcher);
    ~AppInstanceMessageHandlerLinux() override;

    /**
     * @brief Late initialization, called after Mainframe is created. 
     * Not needed in this implementation.
     */
    void init(void* window_handle) override;

    /**
     * @brief Uses AbstractAppInstanceMessageSender to multicast a message. Adds instance hash.
     */
    void multicast_message(const std::string& message_type, const std::string& message_data) override;
    
    /**
     * @brief Every dbus message received comes her.
     */
    DBusHandlerResult broadcast_listener_handle_dbus_object_message(DBusConnection *connection, DBusMessage *message, void *user_data);

    /**
     * @brief every dbus message received comes here
     */
    DBusHandlerResult multicast_listener_handle_dbus_object_message(DBusConnection *connection, DBusMessage *message, void *user_data);
    
    /**
     * @brief Called handle_message_type_other_closed gets false from anther instance running. Linux does reregister dbus broadcast listener on this. 
     */
    void on_becoming_primary_instance() override;

private:
    // instance check worker thread to listen incoming dbus communication
    // Only one instance has registered dbus object at time
    JThread::JThread        m_broadcast_listener_thread;
    std::condition_variable m_broadcast_listener_thread_stop_condition;
    mutable std::mutex      m_broadcast_listener_thread_stop_mutex;
    void    listen_broadcast(JThread::StopToken stop_token);

    /**
     * Reply to introspect makes our DBus object visible for other programs like D-Feet.
     */
    void broadcast_listener_respond_to_introspect(DBusConnection *connection, DBusMessage *request);

    /**
     * Method AnotherInstance receives message from another PrusaSlicer instance.
     */
    void broadcast_listener_handle_message(DBusConnection *connection, DBusMessage *request);


    // "multicast" worker thread to listen incoming dbus communication
    // every instance has registered its own object
    JThread::JThread        m_multicast_listener_thread;
    std::condition_variable m_multicast_listener_thread_stop_condition;
    mutable std::mutex      m_multicast_listener_thread_stop_mutex;
    void    listen_multicast(JThread::StopToken stop_token);

    /**
     * @brief reply to introspect makes our DBus object visible for other programs like D-Feet
     */
    void multicast_listener_respond_to_introspect(DBusConnection *connection, DBusMessage *request);

    /**
     * @brief method Message receives message from another PrusaSlicer instance 
     */
    void multicast_listener_handle_message(DBusConnection *connection, DBusMessage *request);
};

}