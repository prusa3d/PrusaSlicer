///|/ Copyright (c) Prusa Research 2020 - 2023 David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GUI_App.hpp"
#include "InstanceCheck.hpp"
#include "Plater.hpp"
#include "format.hpp"

#ifdef _WIN32
  #include "MainFrame.hpp"
#endif

#include "libslic3r/Utils.hpp"
#include "libslic3r/Config.hpp"

#include "boost/nowide/convert.hpp"
#include <boost/log/trivial.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <iostream>
#include <unordered_map>
#include <fcntl.h>
#include <errno.h>
#include <optional>
#include <cstdint>
#include <regex>

#ifdef _WIN32
#include <strsafe.h>
#endif //WIN32

#if __linux__
#include <dbus/dbus.h> /* Pull in all of D-Bus headers. */
#endif //__linux__

namespace Slic3r {

#ifdef __APPLE__
	bool unlock_lockfile(const std::string& name, const std::string& path)
	{
		std::string dest_dir = path + name;
		//BOOST_LOG_TRIVIAL(debug) << "full lock path: " << dest_dir;
		struct      flock fl;
		int         fdlock;
		fl.l_type = F_UNLCK;
		fl.l_whence = SEEK_SET;
		fl.l_start = 0;
		fl.l_len = 1;
		if ((fdlock = open(dest_dir.c_str(), O_WRONLY | O_CREAT, 0666)) == -1)
			return false;

		if (fcntl(fdlock, F_SETLK, &fl) == -1)
			return false;

		return true;
	}
#endif //__APPLE__

namespace instance_check_internal
{
	static bool        s_created_lockfile = false;

	struct CommandLineAnalysis
	{
		std::optional<bool>	should_send;
		std::string    		cl_string;
	};
	static CommandLineAnalysis process_command_line(int argc, char** argv)
	{
		CommandLineAnalysis ret;
        std::vector<std::string> arguments { argv[0] };
		bool send_if_url = false;
		bool has_url = false;
        for (int i = 1; i < argc; ++i) {
			const std::string token = argv[i];
			if (token.find("prusaslicer://") == 0) {
				BOOST_LOG_TRIVIAL(info) << "url found: " << token;
				has_url = true;
			}
			// Processing of boolean command line arguments shall match DynamicConfig::read_cli().
			if (token == "--single-instance")
				ret.should_send = true;
			else if (token == "--no-single-instance")
				ret.should_send = false;
			else if (token == "--single-instance-on-url")
				send_if_url = true;
			else
				arguments.emplace_back(token);
		} 
		if (send_if_url && has_url) {
			ret.should_send = true;
		}
        // We do now want escape_strings_cstyle that quotes strings
        // It would not be possible to use inside json
        for (const std::string& arg : arguments) {
            ret.cl_string += escape_string_cstyle(arg);
            ret.cl_string += ";";
        }
		BOOST_LOG_TRIVIAL(info) << "single instance: " << 
            (ret.should_send.has_value() ? (*ret.should_send ? "true" : "false") : "undefined") <<
			". other params: " << ret.cl_string;
		return ret;
	}

	std::string compose_message_json(const std::string& type, const std::string& data)
    {
        return GUI::format("{ \"type\" : \"%1%\" , \"data\" : \"%2%\"}", type, data);
    }

#ifdef _WIN32

	static HWND l_prusa_slicer_hwnd;
	static BOOL CALLBACK EnumWindowsProc(_In_ HWND   hwnd, _In_ LPARAM lParam)
	{
		//checks for other instances of prusaslicer, if found brings it to front and return false to stop enumeration and quit this instance
		//search is done by classname(wxWindowNR is wxwidgets thing, so probably not unique) and name in window upper panel
		//other option would be do a mutex and check for its existence
		//BOOST_LOG_TRIVIAL(error) << "ewp: version: " << l_version_wstring;
		TCHAR 		 wndText[1000];
		TCHAR 		 className[1000];
		int          err;
		err = GetClassName(hwnd, className, 1000);
		if (err == 0)
			return true;
		err = GetWindowText(hwnd, wndText, 1000);
		if (err == 0)
			return true;
		std::wstring classNameString(className);
		std::wstring wndTextString(wndText);
		if (wndTextString.find(L"PrusaSlicer") != std::wstring::npos && classNameString == L"wxWindowNR") {
			//check if other instances has same instance hash
			//if not it is not same version(binary) as this version 
			HANDLE   handle = GetProp(hwnd, L"Instance_Hash_Minor");
			uint64_t other_instance_hash = PtrToUint(handle);
			uint64_t other_instance_hash_major;
			uint64_t my_instance_hash = GUI::wxGetApp().get_instance_hash_int();
			handle = GetProp(hwnd, L"Instance_Hash_Major");
			other_instance_hash_major = PtrToUint(handle);
			other_instance_hash_major = other_instance_hash_major << 32;
			other_instance_hash += other_instance_hash_major;
			handle = GetProp(hwnd, L"Instance_Is_Maximized");
			const bool maximized = PtrToUint(handle) == 1;

			if(my_instance_hash == other_instance_hash)
			{
				BOOST_LOG_TRIVIAL(debug) << "win enum - found correct instance";
				l_prusa_slicer_hwnd = hwnd;
				ShowWindow(hwnd, maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
				SetForegroundWindow(hwnd);
				return false;
			}
			BOOST_LOG_TRIVIAL(debug) << "win enum - found wrong instance";
		}
		return true;
	}
	static bool send_message(const std::string& message, const std::string &version)
	{
		if (!EnumWindows(EnumWindowsProc, 0)) {
			std::wstring wstr = boost::nowide::widen(message);
			std::unique_ptr<LPWSTR> command_line_args = std::make_unique<LPWSTR>(const_cast<LPWSTR>(wstr.c_str()));
			/*LPWSTR command_line_args = new wchar_t[wstr.size() + 1];
			copy(wstr.begin(), wstr.end(), command_line_args);
			command_line_args[wstr.size()] = 0;*/

			//Create a COPYDATASTRUCT to send the information
			//cbData represents the size of the information we want to send.
			//lpData represents the information we want to send.
			//dwData is an ID defined by us(this is a type of ID different than WM_COPYDATA).
			COPYDATASTRUCT data_to_send = { 0 };
			data_to_send.dwData = 1;
			data_to_send.cbData = sizeof(TCHAR) * (wcslen(*command_line_args.get()) + 1);
			data_to_send.lpData = *command_line_args.get();
			SendMessage(l_prusa_slicer_hwnd, WM_COPYDATA, 0, (LPARAM)&data_to_send);
			return true;  
		}
	    return false;
	}

    static BOOL CALLBACK enum_windows_process_multicast(_In_ HWND   hwnd, _In_ LPARAM lParam)
	{
        if (hwnd == GUI::wxGetApp().mainframe->GetHandle()) {
            return true;
        }
		
		TCHAR 		 wndText[1000];
		TCHAR 		 className[1000];
		int          err;
		err = GetClassName(hwnd, className, 1000);
		if (err == 0)
			return true;
		err = GetWindowText(hwnd, wndText, 1000);
		if (err == 0)
			return true;
		std::wstring classNameString(className);
		std::wstring wndTextString(wndText);
		if (wndTextString.find(L"PrusaSlicer") != std::wstring::npos && classNameString == L"wxWindowNR") {
			//check if other instances has same instance hash
			//if not it is not same version(binary) as this version 
			HANDLE   handle = GetProp(hwnd, L"Instance_Hash_Minor");
			uint64_t other_instance_hash = PtrToUint(handle);
			uint64_t other_instance_hash_major;
			uint64_t my_instance_hash = GUI::wxGetApp().get_instance_hash_int();
			handle = GetProp(hwnd, L"Instance_Hash_Major");
			other_instance_hash_major = PtrToUint(handle);
			other_instance_hash_major = other_instance_hash_major << 32;
			other_instance_hash += other_instance_hash_major;
			handle = GetProp(hwnd, L"Instance_Is_Maximized");
			const bool maximized = PtrToUint(handle) == 1;

			if (my_instance_hash == other_instance_hash)
			{
				BOOST_LOG_TRIVIAL(debug) << "win multicast enum - found instance " << hwnd;
                std::wstring multicast_message = *reinterpret_cast<std::wstring*>(lParam);
                std::unique_ptr<LPWSTR> message = std::make_unique<LPWSTR>(const_cast<LPWSTR>(multicast_message.c_str()));
                
			    //Create a COPYDATASTRUCT to send the information
			    //cbData represents the size of the information we want to send.
			    //lpData represents the information we want to send.
			    //dwData is an ID defined by us(this is a type of ID different than WM_COPYDATA).
			    COPYDATASTRUCT data_to_send = { 0 };
			    data_to_send.dwData = 1;
			    data_to_send.cbData = sizeof(TCHAR) * (wcslen(*message.get()) + 1);
			    data_to_send.lpData = *message.get();
			    SendMessage(hwnd, WM_COPYDATA, 0, (LPARAM)&data_to_send);

			    return true;
			}
			BOOST_LOG_TRIVIAL(trace) << "win enum - found wrong instance";
		}
		return true;
	}

    static void multicast_message_inner(const std::string& message)
	{
        // multicast_message must live until EnumWindows is done, it is passed as pointer parameter.
        std::wstring multicast_message = boost::nowide::widen(message);
        EnumWindows(enum_windows_process_multicast, reinterpret_cast<LPARAM>(&multicast_message));
	}

#else 

	static bool get_lock(const std::string& name, const std::string& path)
	{
		std::string dest_dir = path + name;
		BOOST_LOG_TRIVIAL(debug) <<"full lock path: "<< dest_dir;
		struct      flock fl;
		int         fdlock;
		fl.l_type = F_WRLCK;
		fl.l_whence = SEEK_SET;
		fl.l_start = 0;
		fl.l_len = 1;

        if (! boost::filesystem::is_directory(path)) {
            BOOST_LOG_TRIVIAL(debug) << "get_lock(): datadir does not exist yet, creating...";
            if (! boost::filesystem::create_directories(path))
                BOOST_LOG_TRIVIAL(debug) << "get_lock(): unable to create datadir !!!";
        }

		if ((fdlock = open(dest_dir.c_str(), O_WRONLY | O_CREAT, 0666)) == -1) {
			BOOST_LOG_TRIVIAL(debug) << "Not creating lockfile.";
			return true;
		}

		if (fcntl(fdlock, F_SETLK, &fl) == -1) {
			BOOST_LOG_TRIVIAL(debug) << "Not creating lockfile.";
			return true;
		}

		BOOST_LOG_TRIVIAL(debug) << "Creating lockfile.";
		s_created_lockfile = true;
		return false;
	}

	// Deletes lockfile if it was created by this instance
	// The Lockfile is created only on Linux a OSX. On Win, its handled by named mutex.
	// The lockfile is deleted by instance it created it.
	// On OSX message is passed to other instances to create a new lockfile after deletition.
	static void delete_lockfile()
	{
		//BOOST_LOG_TRIVIAL(debug) << "shuting down with lockfile: " << l_created_lockfile;
		if (s_created_lockfile)
		{
			std::string path = data_dir() + "/cache/" + GUI::wxGetApp().get_instance_hash_string() + ".lock";
			if( remove( path.c_str() ) != 0 )
	   			BOOST_LOG_TRIVIAL(error) << "Failed to delete lockfile " << path;
	  		//else
	    	//	BOOST_LOG_TRIVIAL(error) << "success delete lockfile " << path;
#ifdef __APPLE__
			// Partial fix of #7583
			// On price of incorrect working of single instances on older OSX
			if (wxPlatformInfo::Get().GetOSMajorVersion() > 12)
	   			send_message_mac_closing(GUI::wxGetApp().get_instance_hash_string(),GUI::wxGetApp().get_instance_hash_string());
#endif	    
		}
	}

#endif //WIN32
#if defined(__APPLE__)

	static void multicast_message_inner(const std::string &message_text)
	{
		multicast_message_mac(message_text);
	}

	static bool send_message(const std::string &message_text, const std::string &version)
	{
		//std::string v(version);
		//std::replace(v.begin(), v.end(), '.', '-');
		//if (!instance_check_internal::get_lock(v)) 
		{
			send_message_mac(message_text, version);
			return true;
		}
		return false;
	}

#elif defined(__linux__)

	static void list_matching_objects(const std::string& pattern, std::vector<std::string>& result) 
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
	    DBusConnection* connection;
	    DBusError error;

	    // Initialize the D-Bus error.
	    dbus_error_init(&error);

	    // Connect to the session bus.
	    connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
	    if (!connection) {
	        BOOST_LOG_TRIVIAL(error) << "Failed to connect to the D-Bus session bus: " << error.message;
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
	        BOOST_LOG_TRIVIAL(error) << "Failed to create D-Bus message.";
	        return;
	    }

	    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, -1, &error);
	    dbus_message_unref(message);

	    if (!reply) {
	        BOOST_LOG_TRIVIAL(error) << "Failed to send message: " << error.message;
	        dbus_error_free(&error);
	        return;
	    }

	    // Parse the reply.
	    DBusMessageIter args;
	    if (!dbus_message_iter_init(reply, &args)) {
	        BOOST_LOG_TRIVIAL(error) << "Reply does not contain arguments.";
	        dbus_message_unref(reply);
	        return;
	    }

	    if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY) {
	        BOOST_LOG_TRIVIAL(error) << "Unexpected argument type in reply.";
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
	            BOOST_LOG_TRIVIAL(debug) << "Matching object found: " << name;
	        }
	        dbus_message_iter_next(&array_iter);
	    }

	    dbus_message_unref(reply);
	    dbus_error_free(&error);
	}
	

	static bool multicast_one_message(const std::string &message_text, const std::string &interface_name)
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " to " << interface_name;
		DBusMessage* msg;
		DBusConnection* conn;
		DBusError 		err;
		dbus_uint32_t 	serial = 0;
		const char* sigval = message_text.c_str();
		std::string   	method_name = "Message";
		//std::string		interface_name = "com.prusa3d.prusaslicer.MulticastListener.Object" + reciever_id;
		//std::string		object_name = "/com/prusa3d/prusaslicer/MulticastListener/Object" + reciever_id;
		std::string		object_name = "/" + interface_name;
		std::replace(object_name.begin(), object_name.end(), '.', '/');

        // initialise the error value
		dbus_error_init(&err);
		// connect to bus, and check for errors (use SESSION bus everywhere!)
		conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
		if (dbus_error_is_set(&err)) {
			BOOST_LOG_TRIVIAL(error) << "DBus Connection Error. Message to another instance wont be send.";
			BOOST_LOG_TRIVIAL(error) << "DBus Connection Error: " << err.message;
			dbus_error_free(&err);
			return false;
		}
		if (NULL == conn) {
			BOOST_LOG_TRIVIAL(error) << "DBus Connection is NULL. Message to another instance wont be send.";
			return false;
		}
		//some sources do request interface ownership before constructing msg but i think its wrong.
		//create new method call message
		msg = dbus_message_new_method_call(interface_name.c_str(), object_name.c_str(), interface_name.c_str(), method_name.c_str());
		if (NULL == msg) {
			BOOST_LOG_TRIVIAL(error) << "DBus Message is NULL. Message to another instance wont be send.";
			dbus_connection_unref(conn);
			return false;
		}
		//the Message method is not sending reply.
		dbus_message_set_no_reply(msg, TRUE);
		//append arguments to message
		if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &sigval, DBUS_TYPE_INVALID)) {
			BOOST_LOG_TRIVIAL(error) << "Ran out of memory while constructing args for DBus message. Message to another instance wont be send.";
			dbus_message_unref(msg);
			dbus_connection_unref(conn);
			return false;
		}
		// send the message and flush the connection
		if (!dbus_connection_send(conn, msg, &serial)) {
			BOOST_LOG_TRIVIAL(error) << "Ran out of memory while sending DBus message.";
			dbus_message_unref(msg);
			dbus_connection_unref(conn);
			return false;
		}
		dbus_connection_flush(conn);
		BOOST_LOG_TRIVIAL(trace) << "DBus message sent.";
		// free the message and close the connection
		dbus_message_unref(msg);                                                                                                                                                                                    
		dbus_connection_unref(conn);
		return true;
	}

	static void multicast_message_inner(const std::string &message_text)
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
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
					BOOST_LOG_TRIVIAL(error) << "Failed send DBUS message to " << instance;
				} else {
					BOOST_LOG_TRIVIAL(debug) << "Successfully sent DBUS message to " << instance;
				}
		    }
		}
		
	}


	static bool  send_message(const std::string &message_text, const std::string &version)
	{
		/*std::string v(version);
		std::replace(v.begin(), v.end(), '.', '-');
		if (!instance_check_internal::get_lock(v))*/
		/*auto checker = new wxSingleInstanceChecker;
		if ( !checker->IsAnotherRunning() ) */
		{
			DBusMessage* msg;
            // DBusMessageIter args;
			DBusConnection* conn;
			DBusError 		err;
			dbus_uint32_t 	serial = 0;
			const char* sigval = message_text.c_str();
			//std::string		interface_name = "com.prusa3d.prusaslicer.InstanceCheck";
			std::string		interface_name = "com.prusa3d.prusaslicer.InstanceCheck.Object" + version;
			std::string   	method_name = "AnotherInstance";
			//std::string		object_name = "/com/prusa3d/prusaslicer/InstanceCheck";
			std::string		object_name = "/com/prusa3d/prusaslicer/InstanceCheck/Object" + version;


			// initialise the error value
			dbus_error_init(&err);

			// connect to bus, and check for errors (use SESSION bus everywhere!)
			conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
			if (dbus_error_is_set(&err)) {
				BOOST_LOG_TRIVIAL(error) << "DBus Connection Error. Message to another instance wont be send.";
				BOOST_LOG_TRIVIAL(error) << "DBus Connection Error: " << err.message;
				dbus_error_free(&err);
				return true;
			}
			if (NULL == conn) {
				BOOST_LOG_TRIVIAL(error) << "DBus Connection is NULL. Message to another instance wont be send.";
				return true;
			}

			//some sources do request interface ownership before constructing msg but i think its wrong.

			//create new method call message
			msg = dbus_message_new_method_call(interface_name.c_str(), object_name.c_str(), interface_name.c_str(), method_name.c_str());
			if (NULL == msg) {
				BOOST_LOG_TRIVIAL(error) << "DBus Message is NULL. Message to another instance wont be send.";
				dbus_connection_unref(conn);
				return true;
			}
			//the AnotherInstance method is not sending reply.
			dbus_message_set_no_reply(msg, TRUE);

			//append arguments to message
			if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &sigval, DBUS_TYPE_INVALID)) {
				BOOST_LOG_TRIVIAL(error) << "Ran out of memory while constructing args for DBus message. Message to another instance wont be send.";
				dbus_message_unref(msg);
				dbus_connection_unref(conn);
				return true;
			}

			// send the message and flush the connection
			if (!dbus_connection_send(conn, msg, &serial)) {
				BOOST_LOG_TRIVIAL(error) << "Ran out of memory while sending DBus message.";
				dbus_message_unref(msg);
				dbus_connection_unref(conn);
				return true;
			}
			dbus_connection_flush(conn);

			BOOST_LOG_TRIVIAL(trace) << "DBus message sent.";

			// free the message and close the connection
			dbus_message_unref(msg);                                                                                                                                                                                    
			dbus_connection_unref(conn);
			return true;
		}
		return false;
	}

#endif //__APPLE__/__linux__
} //namespace instance_check_internal

bool instance_check(int argc, char** argv, bool app_config_single_instance)
{
	std::size_t hashed_path;
#ifdef _WIN32
	hashed_path = std::hash<std::string>{}(boost::filesystem::system_complete(argv[0]).string());
#else
	boost::system::error_code ec;
#ifdef __linux__
	// If executed by an AppImage, start the AppImage, not the main process.
	// see https://docs.appimage.org/packaging-guide/environment-variables.html#id2
	const char *appimage_env = std::getenv("APPIMAGE");
	bool appimage_env_valid = false;
	if (appimage_env) {
		try {
			auto appimage_path = boost::filesystem::canonical(boost::filesystem::path(appimage_env));
			if (boost::filesystem::exists(appimage_path)) {
				hashed_path = std::hash<std::string>{}(appimage_path.string());
				appimage_env_valid = true;
			}
		} catch (std::exception &) {			
		}
		if (! appimage_env_valid)
			BOOST_LOG_TRIVIAL(error) << "APPIMAGE environment variable was set, but it does not point to a valid file: " << appimage_env;
	}
	if (! appimage_env_valid)
#endif // __linux__
		hashed_path = std::hash<std::string>{}(boost::filesystem::canonical(boost::filesystem::system_complete(argv[0]), ec).string());
	if (ec.value() > 0) { // canonical was not able to find the executable (can happen with appimage on some systems. Does it fail on Fuse file systems?)
		ec.clear();
		// Compose path with boost canonical of folder and filename
		hashed_path = std::hash<std::string>{}(boost::filesystem::canonical(boost::filesystem::system_complete(argv[0]).parent_path(), ec).string() + "/" + boost::filesystem::system_complete(argv[0]).filename().string());
		if (ec.value() > 0) {
			// Still not valid, process without canonical
			hashed_path = std::hash<std::string>{}(boost::filesystem::system_complete(argv[0]).string());
		}
	}
#endif // _WIN32

	std::string lock_name 	= std::to_string(hashed_path);
	GUI::wxGetApp().set_instance_hash(hashed_path);
	BOOST_LOG_TRIVIAL(debug) <<"full path: "<< lock_name;
	instance_check_internal::CommandLineAnalysis cla = instance_check_internal::process_command_line(argc, argv);
	if (! cla.should_send.has_value())
		cla.should_send = app_config_single_instance;
#ifdef _WIN32
	GUI::wxGetApp().init_single_instance_checker(lock_name + ".lock", data_dir() + "\\cache\\");
	if (cla.should_send.value() && GUI::wxGetApp().single_instance_checker()->IsAnotherRunning()) {
#else // mac & linx
	// get_lock() creates the lockfile therefore *cla.should_send is checked after
	if (instance_check_internal::get_lock(lock_name + ".lock", data_dir() + "/cache/") && *cla.should_send) {
#endif
        instance_check_internal::send_message(instance_check_internal::compose_message_json("CLI", cla.cl_string), lock_name);
		BOOST_LOG_TRIVIAL(error) << "Instance check: Another instance found. This instance will terminate. Lock file of current running instance is located at " << data_dir() << 
#ifdef _WIN32
			"\\cache\\"
#else // mac & linx
			"/cache/"
#endif
			<< lock_name << ".lock";
		return true;
	}
	BOOST_LOG_TRIVIAL(info) << "Instance check: Another instance not found or single-instance not set.";
	
	return false;
}


namespace GUI {

wxDEFINE_EVENT(EVT_LOAD_MODEL_OTHER_INSTANCE, LoadFromOtherInstanceEvent);
wxDEFINE_EVENT(EVT_START_DOWNLOAD_OTHER_INSTANCE, StartDownloadOtherInstanceEvent);
wxDEFINE_EVENT(EVT_LOGIN_OTHER_INSTANCE, LoginOtherInstanceEvent);
wxDEFINE_EVENT(EVT_INSTANCE_GO_TO_FRONT, InstanceGoToFrontEvent);
wxDEFINE_EVENT(EVT_STORE_READ_REQUEST, SimpleEvent);

void OtherInstanceMessageHandler::init(wxEvtHandler* callback_evt_handler)
{
	assert(!m_initialized);
	assert(m_callback_evt_handler == nullptr);
	if (m_initialized) 
		return;

	m_initialized = true;
	m_callback_evt_handler = callback_evt_handler;

#if defined(__APPLE__)
	this->register_for_messages(wxGetApp().get_instance_hash_string());
#endif //__APPLE__

#ifdef BACKGROUND_MESSAGE_LISTENER
	m_instance_check_thread = boost::thread((boost::bind(&OtherInstanceMessageHandler::listen_instance_check, this)));
	m_multicast_listener_thread = boost::thread((boost::bind(&OtherInstanceMessageHandler::listen_multicast, this)));
#endif //BACKGROUND_MESSAGE_LISTENER
}
void OtherInstanceMessageHandler::shutdown(MainFrame* main_frame)
{
	BOOST_LOG_TRIVIAL(debug) << "message handler shutdown().";
#ifndef _WIN32
	instance_check_internal::delete_lockfile();
#endif //!_WIN32
	assert(m_initialized);
	if (m_initialized) {
#ifdef _WIN32
		HWND hwnd = main_frame->GetHandle();
		RemoveProp(hwnd, L"Instance_Hash_Minor");
		RemoveProp(hwnd, L"Instance_Hash_Major");
		RemoveProp(hwnd, L"Instance_Is_Maximized");
#endif //_WIN32
#if __APPLE__
		//delete macos implementation
		this->unregister_for_messages();
#endif //__APPLE__
#ifdef BACKGROUND_MESSAGE_LISTENER
		if (m_instance_check_thread.joinable()) {
			// Stop the worker thread, if running.
			{
				// Notify the worker thread to cancel wait on detection polling.
				std::lock_guard<std::mutex> lck(m_instance_check_thread_stop_mutex);
				m_instance_check_thread_stop = true;
			}
			m_instance_check_thread_stop_condition.notify_all();
			// Wait for the worker thread to stop.
			m_instance_check_thread.join();
			m_instance_check_thread_stop = false;
		}

		if (m_multicast_listener_thread.joinable()) {
			// Stop the worker thread, if running.
			{
				// Notify the worker thread to cancel wait on detection polling.
				std::lock_guard<std::mutex> lck(m_multicast_listener_thread_stop_mutex);
				m_multicast_listener_thread_stop = true;
			}
			m_multicast_listener_thread_stop_condition.notify_all();
			// Wait for the worker thread to stop.
			m_multicast_listener_thread.join();
			m_multicast_listener_thread_stop = false;
		}
#endif //BACKGROUND_MESSAGE_LISTENER
		m_callback_evt_handler = nullptr;
		m_initialized = false;
	}
}

#ifdef _WIN32 
void OtherInstanceMessageHandler::init_windows_properties(MainFrame* main_frame, size_t instance_hash)
{
	size_t       minor_hash = instance_hash & 0xFFFFFFFF;
	size_t       major_hash = (instance_hash & 0xFFFFFFFF00000000) >> 32;
	size_t       is_maximized = main_frame->IsMaximized() ? 1 : 0;
	HWND         hwnd = main_frame->GetHandle();
	HANDLE       handle_minor = UIntToPtr(minor_hash);
	HANDLE       handle_major = UIntToPtr(major_hash);
	HANDLE       handle_is_maximized = UIntToPtr(is_maximized);
	SetProp(hwnd, L"Instance_Hash_Minor", handle_minor);
	SetProp(hwnd, L"Instance_Hash_Major", handle_major);
	SetProp(hwnd, L"Instance_Is_Maximized", handle_is_maximized);
	//BOOST_LOG_TRIVIAL(debug) << "window properties initialized " << instance_hash << " (" << minor_hash << " & "<< major_hash;
}

void OtherInstanceMessageHandler::update_windows_properties(MainFrame* main_frame)
{
	if (m_initialized) {
		// dlete old value of "Instance_Is_Maximized" property
		HWND hwnd = main_frame->GetHandle();
		RemoveProp(hwnd, L"Instance_Is_Maximized");
		// set new value for "Instance_Is_Maximized" property
		size_t	is_maximized		= main_frame->IsMaximized() ? 1 : 0;
		HANDLE	handle_is_maximized	= UIntToPtr(is_maximized);
		SetProp(hwnd, L"Instance_Is_Maximized", handle_is_maximized);
	}
}

#if 0

void OtherInstanceMessageHandler::print_window_info(HWND hwnd)
{
	std::wstring instance_hash = boost::nowide::widen(wxGetApp().get_instance_hash_string());
	TCHAR 		 wndText[1000];
	TCHAR 		 className[1000];
	GetClassName(hwnd, className, 1000);
	GetWindowText(hwnd, wndText, 1000);
	std::wstring classNameString(className);
	std::wstring wndTextString(wndText);
	HANDLE       handle = GetProp(hwnd, L"Instance_Hash_Minor");
	size_t       result = PtrToUint(handle);
	handle = GetProp(hwnd, L"Instance_Hash_Major");
	size_t       r2 = PtrToUint(handle);
	r2 = (r2 << 32);
	result += r2;
	BOOST_LOG_TRIVIAL(info) << "window info: " << result;
}
#endif //0
#endif  //WIN32
namespace MessageHandlerInternal
{
   // returns ::path to possible model or empty ::path if input string is not existing path
	static boost::filesystem::path get_path(const std::string& possible_path)
	{
		BOOST_LOG_TRIVIAL(debug) << "message part:" << possible_path;

		if (possible_path.empty() || possible_path.size() < 3) {
			BOOST_LOG_TRIVIAL(debug) << "empty";
			return boost::filesystem::path();
		}
		if (boost::filesystem::exists(possible_path)) {
			BOOST_LOG_TRIVIAL(debug) << "is path";
			return boost::filesystem::path(possible_path);
		} else if (possible_path[0] == '\"') {
			if(boost::filesystem::exists(possible_path.substr(1, possible_path.size() - 2))) {
				BOOST_LOG_TRIVIAL(debug) << "is path in quotes";
				return boost::filesystem::path(possible_path.substr(1, possible_path.size() - 2));
			}
		}
		BOOST_LOG_TRIVIAL(debug) << "is NOT path";
		return boost::filesystem::path();
	}
} //namespace MessageHandlerInternal

void OtherInstanceMessageHandler::multicast_message(const std::string& message_type, const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << message_type;
    instance_check_internal::multicast_message_inner(instance_check_internal::compose_message_json(message_type, message_data));
}


void OtherInstanceMessageHandler::handle_message_type_cli(const std::string& data)
{
    std::vector<std::string> args;
	bool parsed = unescape_strings_cstyle(data, args);
	assert(parsed);
	if (! parsed) {
		BOOST_LOG_TRIVIAL(error) << "message from other instance is incorrectly formatted: " << data;
		return;
	}

	std::vector<boost::filesystem::path> paths;
	std::vector<std::string> downloads;
	// Skip the first argument, it is the path to the slicer executable.
	auto it = args.begin();
	for (++ it; it != args.end(); ++ it) {
        BOOST_LOG_TRIVIAL(debug) << *it;
		boost::filesystem::path p = MessageHandlerInternal::get_path(*it);
		if (! p.string().empty())
			paths.emplace_back(p);
#ifdef _WIN32
		else if (it->rfind("prusaslicer://open/?file=", 0) == 0)
#else
	    else if (it->rfind("prusaslicer://open?file=", 0) == 0)
#endif
			downloads.emplace_back(*it);
		else if (it->rfind("prusaslicer://login", 0) == 0) {
			wxPostEvent(m_callback_evt_handler, LoginOtherInstanceEvent(GUI::EVT_LOGIN_OTHER_INSTANCE, std::string(*it)));
		}
	}
	if (! paths.empty()) {
		wxPostEvent(m_callback_evt_handler, LoadFromOtherInstanceEvent(GUI::EVT_LOAD_MODEL_OTHER_INSTANCE, std::vector<boost::filesystem::path>(std::move(paths))));
	}
	if (!downloads.empty()) {
		wxPostEvent(m_callback_evt_handler, StartDownloadOtherInstanceEvent(GUI::EVT_START_DOWNLOAD_OTHER_INSTANCE, std::vector<std::string>(std::move(downloads))));
	}
}
void OtherInstanceMessageHandler::handle_message_type_store_read(const std::string& data)
{
     wxPostEvent(m_callback_evt_handler, SimpleEvent(GUI::EVT_STORE_READ_REQUEST));
}

void OtherInstanceMessageHandler::handle_message(const std::string& message) 
{
	BOOST_LOG_TRIVIAL(info) << "message from other instance: " << message;
    // message in format { "type" : "TYPE", "data" : "data" }
    // types: CLI, STORE_READ
    std::string type;
    std::string data;
    try {
        std::stringstream ss(message);
        boost::property_tree::ptree ptree;
        boost::property_tree::read_json(ss, ptree);
        if (const auto action = ptree.get_optional<std::string>("type"); action) {
            type = *action;
        }
        if (const auto data_opt = ptree.get_optional<std::string>("data"); data_opt) {
            data = *data_opt;
        }
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse other instance message: " << e.what();
        return;
    }
    assert(!type.empty());
    assert(m_message_handlers.find(type) != m_message_handlers.end()); // this assert means there is an message type that has no handling.
    if (m_message_handlers.find(type) != m_message_handlers.end()) {
        m_message_handlers[type](data);
    }
}

#ifdef __APPLE__
void OtherInstanceMessageHandler::handle_message_other_closed() 
{
	instance_check_internal::get_lock(wxGetApp().get_instance_hash_string() + ".lock", data_dir() + "/cache/");
}
#endif //__APPLE__

#ifdef BACKGROUND_MESSAGE_LISTENER

namespace InstanceCheckMessageHandlerDBusInternal
{
	//reply to introspect makes our DBus object visible for other programs like D-Feet
	static void respond_to_introspect(DBusConnection *connection, DBusMessage *request) 
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
	        "   <interface name=\"com.prusa3d.prusaslicer.InstanceCheck\">"
	        "     <method name=\"AnotherInstance\">"
	        "       <arg name=\"data\" direction=\"in\" type=\"s\" />"
	        "     </method>"
	        "	  <method name=\"Introspect\">"
	        "       <arg name=\"data\" direction=\"out\" type=\"s\" />"
	        "     </method>"
	        "   </interface>"
	        " </node>";
	     
	    reply = dbus_message_new_method_return(request);
	    dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_data, DBUS_TYPE_INVALID);
	    dbus_connection_send(connection, reply, NULL);
	    dbus_message_unref(reply);
	}

	//method AnotherInstance receives message from another PrusaSlicer instance 
	static void handle_method_another_instance(DBusConnection *connection, DBusMessage *request)
	{
	    DBusError     err;
	    char*         text = nullptr;
		wxEvtHandler* evt_handler;

	    dbus_error_init(&err);
	    dbus_message_get_args(request, &err, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
	    if (dbus_error_is_set(&err)) {
	    	BOOST_LOG_TRIVIAL(trace) << "Dbus method AnotherInstance received with wrong arguments.";
	    	dbus_error_free(&err);
	        return;
	    }
	    wxGetApp().other_instance_message_handler()->handle_message(text);

		evt_handler = wxGetApp().plater();
		if (evt_handler) {
			wxPostEvent(evt_handler, InstanceGoToFrontEvent(EVT_INSTANCE_GO_TO_FRONT));
		}
	}
	//every dbus message received comes here
	static DBusHandlerResult handle_dbus_object_message(DBusConnection *connection, DBusMessage *message, void *user_data)
	{
		const char* interface_name = dbus_message_get_interface(message);
	    const char* member_name    = dbus_message_get_member(message);
	    std::string our_interface  = "com.prusa3d.prusaslicer.InstanceCheck.Object" + wxGetApp().get_instance_hash_string();
	    BOOST_LOG_TRIVIAL(trace) << "DBus message received: interface: " << interface_name << ", member: " << member_name;
	    if (0 == strcmp("org.freedesktop.DBus.Introspectable", interface_name) && 0 == strcmp("Introspect", member_name)) {		
	        InstanceCheckMessageHandlerDBusInternal::respond_to_introspect(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("AnotherInstance", member_name)) {
	        InstanceCheckMessageHandlerDBusInternal::handle_method_another_instance(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("Introspect", member_name)) {
	         InstanceCheckMessageHandlerDBusInternal::respond_to_introspect(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } 
	    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
} //namespace InstanceCheckMessageHandlerDBusInternal

void OtherInstanceMessageHandler::listen_instance_check()
{
    DBusConnection* 	 conn;
    DBusError 			 err;
    int 				 name_req_val;
    DBusObjectPathVTable vtable;
    std::string 		 instance_hash  = wxGetApp().get_instance_hash_string();
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << instance_hash;
	std::string			 interface_name = "com.prusa3d.prusaslicer.InstanceCheck.Object" + instance_hash;
    std::string			 object_name 	= "/com/prusa3d/prusaslicer/InstanceCheck/Object" + instance_hash;

    //BOOST_LOG_TRIVIAL(debug) << "init dbus listen " << interface_name << " " << object_name;
    dbus_error_init(&err);

    // connect to the bus and check for errors (use SESSION bus everywhere!)
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) { 
	    BOOST_LOG_TRIVIAL(error) << "DBus Connection Error: "<< err.message;
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
        dbus_error_free(&err); 
        return;
    }
    if (NULL == conn) { 
		BOOST_LOG_TRIVIAL(error) << "DBus Connection is NULL. Dbus Messages listening terminating.";
        return;
    }

	// request our name on the bus and check for errors
	name_req_val = dbus_bus_request_name(conn, interface_name.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
	if (dbus_error_is_set(&err)) {
	    BOOST_LOG_TRIVIAL(error) << "DBus Request name Error: "<< err.message; 
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
	    dbus_error_free(&err); 
	    dbus_connection_unref(conn);
	    return;
	}
	if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != name_req_val) {
		BOOST_LOG_TRIVIAL(error) << "Not primary owner of DBus name - probably another PrusaSlicer instance is running.";
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
	    dbus_connection_unref(conn);
	    return;
	}

	// Set callbacks. Unregister function should not be nessary.
	vtable.message_function = InstanceCheckMessageHandlerDBusInternal::handle_dbus_object_message;
    vtable.unregister_function = NULL;

    // register new object - this is our access to DBus
    dbus_connection_try_register_object_path(conn, object_name.c_str(), &vtable, NULL, &err);
   	if ( dbus_error_is_set(&err) ) {
   		BOOST_LOG_TRIVIAL(error) << "DBus Register object Error: "<< err.message; 
	    BOOST_LOG_TRIVIAL(error) << "Dbus Messages listening terminating.";
	    dbus_connection_unref(conn);
		dbus_error_free(&err);
		return;
	}

	BOOST_LOG_TRIVIAL(debug) << "Dbus object "<< object_name <<" registered. Starting listening for messages.";

	for (;;) {
		// Wait for 1 second 
		// Cancellable.
		{
			std::unique_lock<std::mutex> lck(m_instance_check_thread_stop_mutex);
			m_instance_check_thread_stop_condition.wait_for(lck, std::chrono::seconds(1), [this] { return m_instance_check_thread_stop; });
		}
		if (m_instance_check_thread_stop) {
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


namespace MulticastMessageHandlerDBusInternal
{
	//reply to introspect makes our DBus object visible for other programs like D-Feet
	static void respond_to_introspect(DBusConnection *connection, DBusMessage *request) 
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
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
	        "	  <method name=\"Introspect\">"
	        "       <arg name=\"data\" direction=\"out\" type=\"s\" />"
	        "     </method>"
	        "   </interface>"
	        " </node>";
	     
	    reply = dbus_message_new_method_return(request);
	    dbus_message_append_args(reply, DBUS_TYPE_STRING, &introspection_data, DBUS_TYPE_INVALID);
	    dbus_connection_send(connection, reply, NULL);
	    dbus_message_unref(reply);
	}

	//method Message receives message from another PrusaSlicer instance 
	static void handle_method_message(DBusConnection *connection, DBusMessage *request)
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
	    DBusError     err;
	    char*         text = nullptr;
		wxEvtHandler* evt_handler;

	    dbus_error_init(&err);
	    dbus_message_get_args(request, &err, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
	    if (dbus_error_is_set(&err)) {
	    	BOOST_LOG_TRIVIAL(debug) << "Dbus method Message received with wrong arguments.";
	    	dbus_error_free(&err);
	        return;
	    }
	    wxGetApp().other_instance_message_handler()->handle_message(text);
	}

	//every dbus message received comes here
	static DBusHandlerResult handle_dbus_object_message(DBusConnection *connection, DBusMessage *message, void *user_data)
	{
		const char* interface_name = dbus_message_get_interface(message);
	    const char* member_name    = dbus_message_get_member(message);
	    std::string our_interface  = "com.prusa3d.prusaslicer.MulticastListener.Object" + std::to_string(get_current_pid());
	    BOOST_LOG_TRIVIAL(debug) << "DBus message received: interface: " << interface_name << ", member: " << member_name;
	    if (0 == strcmp("org.freedesktop.DBus.Introspectable", interface_name) && 0 == strcmp("Introspect", member_name)) {		
	        MulticastMessageHandlerDBusInternal::respond_to_introspect(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("Message", member_name)) {
	        MulticastMessageHandlerDBusInternal::handle_method_message(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } else if (0 == strcmp(our_interface.c_str(), interface_name) && 0 == strcmp("Introspect", member_name)) {
	         MulticastMessageHandlerDBusInternal::respond_to_introspect(connection, message);
	        return DBUS_HANDLER_RESULT_HANDLED;
	    } 
	    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
} //namespace MulticastMessageHandlerDBusInternal

void OtherInstanceMessageHandler::listen_multicast()
{
    DBusConnection* 	 conn;
    DBusError 			 err;
    int 				 name_req_val;
    DBusObjectPathVTable vtable;
    std::string 		 pid  = std::to_string(get_current_pid());
	std::string			 interface_name = "com.prusa3d.prusaslicer.MulticastListener.Object" + pid;
    std::string			 object_name 	= "/com/prusa3d/prusaslicer/MulticastListener/Object" + pid;

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << interface_name;
    dbus_error_init(&err);

    // connect to the bus and check for errors (use SESSION bus everywhere!)
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) { 
	    BOOST_LOG_TRIVIAL(error) << "listen_multicast: DBus Connection Error: "<< err.message;
	    BOOST_LOG_TRIVIAL(error) << "listen_multicast: Dbus Messages listening terminating.";
        dbus_error_free(&err); 
        return;
    }
    if (NULL == conn) { 
		BOOST_LOG_TRIVIAL(error) << "listen_multicast: DBus Connection is NULL. Dbus Messages listening terminating.";
        return;
    }

	// request our name on the bus and check for errors
	name_req_val = dbus_bus_request_name(conn, interface_name.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
	if (dbus_error_is_set(&err)) {
	    BOOST_LOG_TRIVIAL(error) << "listen_multicast: DBus Request name Error: "<< err.message; 
	    BOOST_LOG_TRIVIAL(error) << "listen_multicast: Dbus Messages listening terminating.";
	    dbus_error_free(&err); 
	    dbus_connection_unref(conn);
	    return;
	}
	if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != name_req_val) {
		BOOST_LOG_TRIVIAL(error) << "listen_multicast: Not primary owner of DBus name - probably another PrusaSlicer instance is running.";
	    BOOST_LOG_TRIVIAL(error) << "listen_multicast: Dbus Messages listening terminating.";
	    dbus_connection_unref(conn);
	    return;
	}

	// Set callbacks. Unregister function should not be nessary.
	vtable.message_function = MulticastMessageHandlerDBusInternal::handle_dbus_object_message;
    vtable.unregister_function = NULL;

    // register new object - this is our access to DBus
    dbus_connection_try_register_object_path(conn, object_name.c_str(), &vtable, NULL, &err);
   	if ( dbus_error_is_set(&err) ) {
   		BOOST_LOG_TRIVIAL(error) << "listen_multicast: DBus Register object Error: "<< err.message; 
	    BOOST_LOG_TRIVIAL(error) << "listen_multicast: Dbus Messages listening terminating.";
	    dbus_connection_unref(conn);
		dbus_error_free(&err);
		return;
	}

	BOOST_LOG_TRIVIAL(debug) << "listen_multicast: Dbus object "<< object_name <<" registered. Starting listening for messages.";

	for (;;) {
		// Wait for 1 second 
		// Cancellable.
		{
			std::unique_lock<std::mutex> lck(m_multicast_listener_thread_stop_mutex);
			m_multicast_listener_thread_stop_condition.wait_for(lck, std::chrono::seconds(1), [this] { return m_multicast_listener_thread_stop; });
		}
		if (m_multicast_listener_thread_stop) {
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
#endif //BACKGROUND_MESSAGE_LISTENER
} // namespace GUI
} // namespace Slic3r
