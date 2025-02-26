///|/ Copyright (c) Prusa Research 2020 - 2023 David Kocík @kocikdav, Roman Beránek @zavorka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_InstanceCheck_hpp_
#define slic3r_InstanceCheck_hpp_

#include "Event.hpp"

#if _WIN32
#include <windows.h>
#endif //_WIN32

#include <string>
#include <map>

#include <boost/filesystem.hpp>

#if __linux__
#include <boost/thread.hpp>
#include <mutex>
#include <condition_variable>
#endif // __linux__


namespace Slic3r {
// checks for other running instances and sends them argv,
// if there is --single-instance argument or AppConfig is set to single_instance=1
// returns true if this instance should terminate
bool    instance_check(int argc, char** argv, bool app_config_single_instance);

#if __APPLE__
// apple implementation of inner functions of instance_check
// in InstanceCheckMac.mm
void    send_message_mac(const std::string& msg, const std::string& version);
void    multicast_message_mac(const std::string &msg);
void    send_message_mac_closing(const std::string& msg, const std::string& version);


bool unlock_lockfile(const std::string& name, const std::string& path);
#endif //__APPLE__

namespace GUI {

class MainFrame;

#if __linux__
    #define BACKGROUND_MESSAGE_LISTENER
#endif // __linux__

using LoadFromOtherInstanceEvent = Event<std::vector<boost::filesystem::path>>;
using StartDownloadOtherInstanceEvent = Event<std::vector<std::string>>;
using LoginOtherInstanceEvent = Event<std::string>;
wxDECLARE_EVENT(EVT_LOAD_MODEL_OTHER_INSTANCE, LoadFromOtherInstanceEvent);
wxDECLARE_EVENT(EVT_START_DOWNLOAD_OTHER_INSTANCE, StartDownloadOtherInstanceEvent);
wxDECLARE_EVENT(EVT_LOGIN_OTHER_INSTANCE, LoginOtherInstanceEvent);
using InstanceGoToFrontEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_INSTANCE_GO_TO_FRONT, InstanceGoToFrontEvent);
wxDECLARE_EVENT(EVT_STORE_READ_REQUEST, SimpleEvent);

class OtherInstanceMessageHandler
{
public:
	OtherInstanceMessageHandler() 
    {
         m_message_handlers["CLI"] = std::bind(&OtherInstanceMessageHandler::handle_message_type_cli, this, std::placeholders::_1);
         m_message_handlers["STORE_READ"] = std::bind(&OtherInstanceMessageHandler::handle_message_type_store_read, this, std::placeholders::_1);
    }
	OtherInstanceMessageHandler(OtherInstanceMessageHandler const&) = delete;
	void operator=(OtherInstanceMessageHandler const&) = delete;
	~OtherInstanceMessageHandler() { assert(!m_initialized); }

	// inits listening, on each platform different. On linux starts background thread
	void    init(wxEvtHandler* callback_evt_handler);
	// stops listening, on linux stops the background thread
	void    shutdown(MainFrame* main_frame);

	// message in format { "type" : "TYPE", "data" : "data" }
	void    handle_message(const std::string& message);

    void    multicast_message(const std::string& message_type, const std::string& message_data = std::string());
#ifdef __APPLE__
	// Messege form other instance, that it deleted its lockfile - first instance to get it will create its own.
	void           handle_message_other_closed();
#endif //__APPLE__
#ifdef _WIN32
	static void    init_windows_properties(MainFrame* main_frame, size_t instance_hash);
	void           update_windows_properties(MainFrame* main_frame);
#endif //WIN32
private:
    //finds paths to models in message(= command line arguments, first should be prusaSlicer executable)
	//and sends them to plater via LoadFromOtherInstanceEvent
	//security of messages: from message all existing paths are proccesed to load model 
	//						win32 - anybody who has hwnd can send message.
	//						mac - anybody who posts notification with name:@"OtherPrusaSlicerTerminating"
	//						linux - instrospectable on dbus
    void    handle_message_type_cli(const std::string& data);
    // Passes information to UI to perform store read
    void    handle_message_type_store_read(const std::string& data);
    std::map<std::string, std::function<void(const std::string&)>> m_message_handlers;

	bool                    m_initialized { false };
	wxEvtHandler*           m_callback_evt_handler { nullptr };

#ifdef BACKGROUND_MESSAGE_LISTENER
	// instance check worker thread to listen incoming dbus communication
	// Only one instance has registered dbus object at time
	boost::thread 			m_instance_check_thread;
	std::condition_variable m_instance_check_thread_stop_condition;
	mutable std::mutex 		m_instance_check_thread_stop_mutex;
	bool 					m_instance_check_thread_stop{ false };
	//bool					m_instance_check_thread_start{ true };	
	void    listen_instance_check();

	// "multicast" worker thread to listen incoming dbus communication
	// every instance has registered its own object
	boost::thread 			m_multicast_listener_thread;
	std::condition_variable m_multicast_listener_thread_stop_condition;
	mutable std::mutex 		m_multicast_listener_thread_stop_mutex;
	bool 					m_multicast_listener_thread_stop{ false };
	void    listen_multicast();
#endif //BACKGROUND_MESSAGE_LISTENER

#if __APPLE__
	//implemented at InstanceCheckMac.mm
	void    register_for_messages(const std::string &version_hash);
	void    unregister_for_messages();
	// Opaque pointer to RemovableDriveManagerMM
	void* m_impl_osx;
public: 
	void    bring_instance_forward();
#endif //__APPLE__

};
} // namespace GUI
} // namespace Slic3r
#endif // slic3r_InstanceCheck_hpp_
