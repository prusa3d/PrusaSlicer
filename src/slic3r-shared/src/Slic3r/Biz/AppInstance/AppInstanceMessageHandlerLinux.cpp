#include "AppInstanceMessageHandlerLinux.hpp"

#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include "Slic3r/LegacyFormat.hpp"
#include "Slic3r/Biz/AppInstance/AppInstanceUtils.hpp"

#include <boost/nowide/convert.hpp>
#include <regex>

namespace Slic3r::Biz::AppInstance {

namespace {
void list_matching_objects(const std::string& pattern, std::vector<std::string>& result) 
{
    DBusConnection* connection;
    DBusError error;

    // Initialize the D-Bus error.
    dbus_error_init(&error);

    // Connect to the session bus.
    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!connection) {
        SPDLOG_ERROR("Failed to connect to the D-Bus session bus: {}", error.message);
        dbus_error_free(&error);
        return;
    }

    // Request a list of all bus names.
    DBusMessage* message = dbus_message_new_method_call(
        "org.freedesktop.DBus",  // Destination (the D-Bus daemon)
        "/org/freedesktop/DBus", // Object path
        "org.freedesktop.DBus",  // Interface
        "ListNames"              // Method
    );

    if (!message) {
        SPDLOG_ERROR("Failed to create D-Bus message.");
        return;
    }

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, -1, &error);
    dbus_message_unref(message);

    if (!reply) {
        SPDLOG_ERROR("Failed to send message: {}", error.message);
        dbus_error_free(&error);
        return;
    }

    // Parse the reply.
    DBusMessageIter args;
    if (!dbus_message_iter_init(reply, &args)) {
        SPDLOG_ERROR("Reply does not contain arguments.");
        dbus_message_unref(reply);
        return;
    }

    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) {
        SPDLOG_ERROR("Unexpected argument type in reply.");
        dbus_message_unref(reply);
        return;
    }

    DBusMessageIter array_iter;
    dbus_message_iter_recurse(&args, &array_iter);

    std::regex instance_regex(pattern);

    while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRING) {
        const char* name;
        dbus_message_iter_get_basic(&array_iter, &name);
        if (std::regex_match(name, instance_regex)) {
            result.push_back(name);
        }
        dbus_message_iter_next(&array_iter);
    }
    dbus_message_unref(reply);
    dbus_error_free(&error);
}

bool multicast_one_message(const std::string &message_text, const std::string &interface_name)
{
    DBusMessage* msg;
    DBusConnection* conn;
    DBusError       err;
    dbus_uint32_t   serial = 0;
    const char* sigval = message_text.c_str();
    std::string     method_name = "Message";
    std::string     object_name = "/" + interface_name;
    std::replace(object_name.begin(), object_name.end(), '.', '/');

    // initialise the error value
    dbus_error_init(&err);

    // connect to bus, and check for errors (use SESSION bus everywhere!)
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        SPDLOG_ERROR("DBus Connection Error. Message to another instance wont be send.");
        SPDLOG_ERROR("DBus Connection Error: {}", err.message);
        dbus_error_free(&err);
        return false;
    }
    if (NULL == conn) {
        SPDLOG_ERROR("DBus Connection is NULL. Message to another instance wont be send.");
        return false;
    }
    //some sources do request interface ownership before constructing msg but i think its wrong.
    //create new method call message
    msg = dbus_message_new_method_call(interface_name.c_str(), object_name.c_str(), interface_name.c_str(), method_name.c_str());
    if (NULL == msg) {
        SPDLOG_ERROR("DBus Message is NULL. Message to another instance wont be send.");
        dbus_connection_unref(conn);
        return false;
    }
    //the Message method is not sending reply.
    dbus_message_set_no_reply(msg, TRUE);
    //append arguments to message
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &sigval, DBUS_TYPE_INVALID)) {
        SPDLOG_ERROR("Ran out of memory while constructing args for DBus message. Message to another instance wont be send.");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return false;
    }
    // send the message and flush the connection
    if (!dbus_connection_send(conn, msg, &serial)) {
        SPDLOG_ERROR("Ran out of memory while sending DBus message.");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return false;
    }
    dbus_connection_flush(conn);
    // free the message and close the connection
    dbus_message_unref(msg);                                                                                                                                                                                    
    dbus_connection_unref(conn);
    return true;
}

std::string compose_message_json(const std::string& type, const std::string& data)
{
    return format("{ \"type\" : \"%1%\" , \"data\" : \"%2%\"}", type, data);
}

} // namespace 

void AppInstanceMessageSenderLinux::multicast_message(const std::string& message_type, const std::string& message_data, size_t instance_hash, void* window_handle)
{
    std::string message_text = compose_message_json(message_type, message_data);
    std::string pattern = R"(com\.prusa3d\.prusaslicer\.MulticastListener\.Object\d+)";
    std::vector<std::string> instances;
    std::string my_pid = std::to_string(get_current_pid());

    list_matching_objects(pattern, instances);
    for (const std::string& instance : instances) {
        // regex object pid to not send message to myself.
        std::regex objectRegex("Object(\\d+)");
        std::smatch match;
        if (std::regex_search(instance, match, objectRegex) && match[1] != my_pid) {
            if (!multicast_one_message(message_text, instance)) {
                SPDLOG_ERROR("Failed send DBUS message to {}", instance);
            } else {
                SPDLOG_INFO("Successfully sent DBUS message to {}", instance);
            }
        }
    }    
}

void AppInstanceMessageSenderLinux::broadcast_message(const std::string& message_type, const std::string& message_data, size_t instance_hash, void* window_handle)
{
    std::string version = std::to_string(Biz::Platform::PlatformServices::instance().app_hash());
    std::string message_text = compose_message_json(message_type, message_data);
    DBusMessage* msg;
    DBusConnection* conn;
    DBusError       err;
    dbus_uint32_t   serial = 0;
    const char* sigval = message_text.c_str();
    std::string     interface_name = "com.prusa3d.prusaslicer.BroadcastListener.Object" + version;
    std::string     method_name = "Message";
    std::string     object_name = "/com/prusa3d/prusaslicer/BroadcastListener/Object" + version;

    // initialise the error value
    dbus_error_init(&err);
    
    // connect to bus, and check for errors (use SESSION bus everywhere!)
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        SPDLOG_ERROR("DBus Connection Error. Message to another instance wont be send.");
        SPDLOG_ERROR("DBus Connection Error: {}", err.message);
        dbus_error_free(&err);
        return;
    }
    if (NULL == conn) {
        SPDLOG_ERROR("DBus Connection is NULL. Message to another instance wont be send.");
        return;
    }
    
    // DK: some sources do request interface ownership before constructing msg but i think its wrong.
    
    // create new method call message
    msg = dbus_message_new_method_call(interface_name.c_str(), object_name.c_str(), interface_name.c_str(), method_name.c_str());
    if (NULL == msg) {
        SPDLOG_ERROR("DBus Message is NULL. Message to another instance wont be send.");
        dbus_connection_unref(conn);
        return;
    }
    // the Message method is not sending reply.
    dbus_message_set_no_reply(msg, TRUE);
    
    // append arguments to message
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &sigval, DBUS_TYPE_INVALID)) {
        SPDLOG_ERROR("Ran out of memory while constructing args for DBus message. Message to another instance wont be send.");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return;
    }
    
    // send the message and flush the connection
    if (!dbus_connection_send(conn, msg, &serial)) {
        SPDLOG_ERROR("Ran out of memory while sending DBus message.");
        dbus_message_unref(msg);
        dbus_connection_unref(conn);
        return;
    }
    dbus_connection_flush(conn);
    
    SPDLOG_INFO("DBus message sent.");
    
    // free the message and close the connection
    dbus_message_unref(msg);                                                                                                                                                                                    
    dbus_connection_unref(conn);
    return;
}

AppInstanceMessageHandlerLinux::AppInstanceMessageHandlerLinux(Platform::IMainThreadDispatcher& dispatcher)
: AbstractAppInstanceMessageHandler(dispatcher)
{
    m_broadcast_listener_thread = JThread::JThread([this](JThread::StopToken st) {
        this->listen_broadcast(st); 
    });
    m_multicast_listener_thread = JThread::JThread([this](JThread::StopToken st) {
        this->listen_multicast(st);
    }); 
}

AppInstanceMessageHandlerLinux::~AppInstanceMessageHandlerLinux()
{
    SPDLOG_INFO(__FUNCTION__);
    if (m_broadcast_listener_thread.joinable()) {
        m_broadcast_listener_thread.request_stop();
        m_broadcast_listener_thread_stop_condition.notify_all();
    }
    if (m_multicast_listener_thread.joinable()) {
        m_multicast_listener_thread.request_stop();
        m_multicast_listener_thread_stop_condition.notify_all();
    }
}

void AppInstanceMessageHandlerLinux::on_becoming_primary_instance()
{
    SPDLOG_INFO(__FUNCTION__);
    // broadcast lister thread should not be in infite loop.
    if (m_broadcast_listener_thread.joinable()) {
        m_broadcast_listener_thread.request_stop();
        m_broadcast_listener_thread_stop_condition.notify_all();
    }
    m_broadcast_listener_thread = JThread::JThread([this](JThread::StopToken st) {
        this->listen_broadcast(st); 
    });
}

void AppInstanceMessageHandlerLinux::init(void* window_handle)
{
}

void AppInstanceMessageHandlerLinux::multicast_message(
    const std::string& message_type,
    const std::string& message_data
)
{
    AppInstanceMessageSenderLinux sender;
    sender.multicast_message(
        message_type,
        message_data,
        Platform::PlatformServices::instance().app_hash(),
        nullptr
    );
}

namespace {
DBusHandlerResult static_broadcast_message_handler(DBusConnection *connection, DBusMessage *message, void *user_data) {
        AppInstanceMessageHandlerLinux *self = static_cast<AppInstanceMessageHandlerLinux*>(user_data);
        ASSERT(self);
        return self->broadcast_listener_handle_dbus_object_message(connection, message, user_data);
}
}

void AppInstanceMessageHandlerLinux::listen_broadcast(JThread::StopToken stop_token)
{
    SPDLOG_INFO(__FUNCTION__);
    DBusConnection*      conn;
    DBusError            err;
    int                  name_req_val;
    DBusObjectPathVTable vtable;
    std::string          instance_hash  = std::to_string(Biz::Platform::PlatformServices::instance().app_hash());
    std::string          interface_name = "com.prusa3d.prusaslicer.BroadcastListener.Object" + instance_hash;
    std::string          object_name    = "/com/prusa3d/prusaslicer/BroadcastListener/Object" + instance_hash;

    dbus_error_init(&err);

    // connect to the bus and check for errors (use SESSION bus everywhere!)
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) { 
        SPDLOG_ERROR("DBus Connection Error: {}", err.message);
        SPDLOG_ERROR("Dbus Messages listening terminating.");
        dbus_error_free(&err); 
        return;
    }
    if (NULL == conn) { 
        SPDLOG_ERROR("DBus Connection is NULL. Dbus Messages listening terminating.");
        return;
    }

    // request our name on the bus and check for errors
    name_req_val = dbus_bus_request_name(conn, interface_name.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
    if (dbus_error_is_set(&err)) {
        SPDLOG_ERROR("DBus Request name Error: {}", err.message); 
        SPDLOG_ERROR("Dbus Messages listening terminating.");
        dbus_error_free(&err); 
        dbus_connection_unref(conn);
        return;
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != name_req_val) {
        SPDLOG_ERROR("Not primary owner of DBus name - probably another PrusaSlicer instance is running.");
        SPDLOG_ERROR("Dbus Messages listening terminating.");
        dbus_connection_unref(conn);
        return;
    }

    // Set callbacks. Unregister function should not be nessary.
    vtable.message_function = static_broadcast_message_handler;
    vtable.unregister_function = NULL;

    // register new object - this is our access to DBus
    dbus_connection_try_register_object_path(conn, object_name.c_str(), &vtable, this, &err);
    if ( dbus_error_is_set(&err) ) {
        SPDLOG_ERROR("DBus Register object Error: "); 
        SPDLOG_ERROR("Dbus Messages listening terminating.");
        dbus_connection_unref(conn);
        dbus_error_free(&err);
        return;
    }

    SPDLOG_INFO("Dbus object {} registered. Starting listening for messages.", object_name);

    for (;;) {
        // Wait for 1 second 
        // Cancellable.
        {
            std::unique_lock<std::mutex> lck(m_broadcast_listener_thread_stop_mutex);
            m_broadcast_listener_thread_stop_condition.wait_for(lck, std::chrono::seconds(1), [stop_token] { return stop_token.stop_requested(); });
        }
        if (stop_token.stop_requested()) {
            // Stop the worker thread.
            break;
        }
        //dispatch should do all the work with incoming messages
        //second parameter is blocking time that funciton waits for new messages
        //that is handled here with our own event loop above
        dbus_connection_read_write_dispatch(conn, 0);
    }
   
    dbus_bus_release_name(conn, interface_name.c_str(), &err);
    dbus_connection_unref(conn);
}

DBusHandlerResult AppInstanceMessageHandlerLinux::broadcast_listener_handle_dbus_object_message(DBusConnection *connection, DBusMessage *message, void *user_data)
{
    const char* interface_name = dbus_message_get_interface(message);
    const char* member_name    = dbus_message_get_member(message);
    std::string our_interface  = "com.prusa3d.prusaslicer.BroadcastListener.Object" + std::to_string(Biz::Platform::PlatformServices::instance().app_hash());
    SPDLOG_INFO("DBus message received: interface: {}, member: {}", interface_name, member_name);
    if (0 == strcmp("org.freedesktop.DBus.Introspectable", interface_name) && 0 == strcmp("Introspect", member_name)) {     
        broadcast_listener_respond_to_introspect(connection, message);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("Message", member_name)) {
        broadcast_listener_handle_message(connection, message);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("Introspect", member_name)) {
         broadcast_listener_respond_to_introspect(connection, message);
        return DBUS_HANDLER_RESULT_HANDLED;
    } 
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


void AppInstanceMessageHandlerLinux::broadcast_listener_respond_to_introspect(DBusConnection *connection, DBusMessage *request) 
{
    DBusMessage *reply;
    const char  *introspection_data =
        " <!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
        "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
        " <!-- dbus-sharp 0.8.1 -->"
        " <node>"
        "   <interface name=\"org.freedesktop.DBus.Introspectable\">"
        "     <method name=\"Introspect\">"
        "       <arg name=\"data\" direction=\"out\" type=\"s\" />"
        "     </method>"
        "   </interface>"
        "   <interface name=\"com.prusa3d.prusaslicer.BroadcastListener\">"
        "     <method name=\"Message\">"
        "       <arg name=\"data\" direction=\"in\" type=\"s\" />"
        "     </method>"
        "     <method name=\"Introspect\">"
        "       <arg name=\"data\" direction=\"out\" type=\"s\" />"
        "     </method>"
        "   </interface>"
        " </node>";
     
    reply = dbus_message_new_method_return(request);
    dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_data, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
}

void AppInstanceMessageHandlerLinux::broadcast_listener_handle_message(DBusConnection *connection, DBusMessage *request)
{
    DBusError     err;
    char*         text = nullptr;

    dbus_error_init(&err);
    dbus_message_get_args(request, &err, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
    if (dbus_error_is_set(&err)) {
        SPDLOG_ERROR("Dbus message received with wrong arguments.");
        dbus_error_free(&err);
        return;
    }
    handle_message(text);
    dispatch_go_to_front();
}




namespace {
DBusHandlerResult static_multicast_message_handler(DBusConnection *connection, DBusMessage *message, void *user_data) {
        AppInstanceMessageHandlerLinux *self = static_cast<AppInstanceMessageHandlerLinux*>(user_data);
        ASSERT(self);
        return self->multicast_listener_handle_dbus_object_message(connection, message, user_data);
}
}

void AppInstanceMessageHandlerLinux::listen_multicast(JThread::StopToken stop_token)
{
    
    DBusConnection*      conn;
    DBusError            err;
    int                  name_req_val;
    DBusObjectPathVTable vtable;
    std::string          pid  = std::to_string(get_current_pid());
    std::string          interface_name = "com.prusa3d.prusaslicer.MulticastListener.Object" + pid;
    std::string          object_name    = "/com/prusa3d/prusaslicer/MulticastListener/Object" + pid;

    dbus_error_init(&err);

    // connect to the bus and check for errors (use SESSION bus everywhere!)
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) { 
        SPDLOG_ERROR("listen_multicast: DBus Connection Error: {}", err.message);
        SPDLOG_ERROR("listen_multicast: Dbus Messages listening terminating.");
        dbus_error_free(&err); 
        return;
    }
    if (NULL == conn) { 
        SPDLOG_ERROR("listen_multicast: DBus Connection is NULL. Dbus Messages listening terminating.");
        return;
    }

    // request our name on the bus and check for errors
    name_req_val = dbus_bus_request_name(conn, interface_name.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
    if (dbus_error_is_set(&err)) {
        SPDLOG_ERROR("listen_multicast: DBus Request name Error: {}", err.message); 
        SPDLOG_ERROR("listen_multicast: Dbus Messages listening terminating.");
        dbus_error_free(&err); 
        dbus_connection_unref(conn);
        return;
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != name_req_val) {
        SPDLOG_ERROR("listen_multicast: Not primary owner of DBus name - probably another PrusaSlicer instance is running.");
        SPDLOG_ERROR("listen_multicast: Dbus Messages listening terminating.");
        dbus_connection_unref(conn);
        return;
    }

    // Set callbacks. Unregister function should not be nessary.
    vtable.message_function = static_multicast_message_handler;
    vtable.unregister_function = NULL;

    // register new object - this is our access to DBus
    dbus_connection_try_register_object_path(conn, object_name.c_str(), &vtable, this, &err);
    if ( dbus_error_is_set(&err) ) {
        SPDLOG_ERROR("listen_multicast: DBus Register object Error: {}", err.message); 
        SPDLOG_ERROR("listen_multicast: Dbus Messages listening terminating.");
        dbus_connection_unref(conn);
        dbus_error_free(&err);
        return;
    }

    SPDLOG_INFO("listen_multicast: Dbus object {} registered. Starting listening for messages.", object_name);

    for (;;) {
        // Wait for 1 second 
        // Cancellable.
        {
            std::unique_lock<std::mutex> lck(m_multicast_listener_thread_stop_mutex);
            m_multicast_listener_thread_stop_condition.wait_for(lck, std::chrono::seconds(1), [stop_token] { return stop_token.stop_requested(); });
        }
        if (stop_token.stop_requested()) {
            // Stop the worker thread.
            break;
        }
        //dispatch should do all the work with incoming messages
        //second parameter is blocking time that funciton waits for new messages
        //that is handled here with our own event loop above
        dbus_connection_read_write_dispatch(conn, 0);
    }
     
    dbus_connection_unref(conn);    
}

void AppInstanceMessageHandlerLinux::multicast_listener_respond_to_introspect(DBusConnection *connection, DBusMessage *request) 
{
    DBusMessage *reply;
    const char  *introspection_data =
        " <!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
        "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
        " <!-- dbus-sharp 0.8.1 -->"
        " <node>"
        "   <interface name=\"org.freedesktop.DBus.Introspectable\">"
        "     <method name=\"Introspect\">"
        "       <arg name=\"data\" direction=\"out\" type=\"s\" />"
        "     </method>"
        "   </interface>"
        "   <interface name=\"com.prusa3d.prusaslicer.MulticastListener\">"
        "     <method name=\"Message\">"
        "       <arg name=\"data\" direction=\"in\" type=\"s\" />"
        "     </method>"
        "     <method name=\"Introspect\">"
        "       <arg name=\"data\" direction=\"out\" type=\"s\" />"
        "     </method>"
        "   </interface>"
        " </node>";
     
    reply = dbus_message_new_method_return(request);
    dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_data, DBUS_TYPE_INVALID);
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
}

void AppInstanceMessageHandlerLinux::multicast_listener_handle_message(DBusConnection *connection, DBusMessage *request)
{
    DBusError     err;
    char*         text = nullptr;

    dbus_error_init(&err);
    dbus_message_get_args(request, &err, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
    if (dbus_error_is_set(&err)) {
        SPDLOG_ERROR("Dbus method Message received with wrong arguments.");
        dbus_error_free(&err);
        return;
    }
    handle_message(text);
}

DBusHandlerResult AppInstanceMessageHandlerLinux::multicast_listener_handle_dbus_object_message(DBusConnection *connection, DBusMessage *message, void *user_data)
{
    const char* interface_name = dbus_message_get_interface(message);
    const char* member_name    = dbus_message_get_member(message);
    std::string our_interface  = "com.prusa3d.prusaslicer.MulticastListener.Object" + std::to_string(get_current_pid());
    SPDLOG_INFO("DBus message received: interface: {}, member: {}", interface_name, member_name);
    if (0 == strcmp("org.freedesktop.DBus.Introspectable", interface_name) && 0 == strcmp("Introspect", member_name)) {     
        multicast_listener_respond_to_introspect(connection, message);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("Message", member_name)) {
        multicast_listener_handle_message(connection, message);
        return DBUS_HANDLER_RESULT_HANDLED;
    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("Introspect", member_name)) {
        multicast_listener_respond_to_introspect(connection, message);
        return DBUS_HANDLER_RESULT_HANDLED;
    } 
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
}
