///|/ Copyright (c) Prusa Research 2022 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena
///|/ Copyright (c) 2022 KARBOWSKI Piotr
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "AppUpdater.hpp"

#include <atomic>
#include <thread>
#include <string>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <curl/curl.h>

#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/Utils/Http.hpp"

#include "libslic3r/Utils.hpp"

#ifdef _WIN32
#include <shellapi.h>
#include <Shlobj_core.h>
#include <windows.h>
#include <KnownFolders.h>
#include <shlobj.h>
#endif // _WIN32


namespace Slic3r {

namespace {
	
#ifdef _WIN32
	bool run_file(const boost::filesystem::path& path)
	{
		std::string msg;
		bool res = GUI::create_process(path, std::wstring(), msg);
		if (!res) {
			std::string full_message = GUI::format(_u8L("Running downloaded instaler of %1% has failed:\n%2%"), SLIC3R_APP_NAME, msg);
			BOOST_LOG_TRIVIAL(error) << full_message;
			wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_FAILED);
			evt->SetString(full_message);
			GUI::wxGetApp().QueueEvent(evt);
		}
		return res;
	}

	std::string get_downloads_path()
	{
		std::string ret;
		PWSTR path = NULL;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);
        if (SUCCEEDED(hr)) {
            ret = boost::nowide::narrow(path);
		}
		CoTaskMemFree(path);
		return ret;
	}
#elif  __APPLE__
	bool run_file(const boost::filesystem::path& path)
	{
		if (boost::filesystem::exists(path)) {
			// attach downloaded dmg file
            const char* argv1[] = { "hdiutil", "attach", path.string().c_str(), nullptr };
            ::wxExecute(const_cast<char**>(argv1), wxEXEC_ASYNC, nullptr);
            // open inside attached as a folder in finder
            const char* argv2[] = { "open", "/Volumes/PrusaSlicer", nullptr };
			::wxExecute(const_cast<char**>(argv2), wxEXEC_ASYNC, nullptr);
			return true;
		}
		return false;
	}

	std::string get_downloads_path()
	{
		// call objective-c implementation
		return get_downloads_path_mac();
	}
#else
	bool run_file(const boost::filesystem::path& path)
	{	
		return false;
	}

	std::string get_downloads_path()
	{
		wxString command = "xdg-user-dir DOWNLOAD";
		wxArrayString output;
		GUI::desktop_execute_get_result(command, output);
		if (output.GetCount() > 0) {
			return output[0].ToUTF8().data(); //lm:I would use wxString::ToUTF8(), although on Linux, nothing at all should work too.
		}
		return std::string();
	}
#endif // _WIN32 / __apple__ / else 
} // namespace

wxDEFINE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_SLIC3R_APP_DOWNLOAD_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_SLIC3R_APP_DOWNLOAD_FAILED, wxCommandEvent);
wxDEFINE_EVENT(EVT_SLIC3R_APP_OPEN_FAILED, wxCommandEvent);

// priv handles all operations in separate thread
// 1) download version file and parse it.
// 2) download new app file and open in folder / run it.
struct AppUpdater::priv {
	priv();
	// Download file. What happens with the data is specified in completefn.
	bool http_get_file(const std::string& url
		, size_t size_limit
		, std::function<bool(Http::Progress)> progress_fn
		, std::function<bool(std::string /*body*/, std::string& error_message)> completefn
		, std::string& error_message
	) const;

	// Download installer / app
	boost::filesystem::path download_file(const DownloadAppData& data) const;
	// Run file in m_last_dest_path
	bool run_downloaded_file(boost::filesystem::path path);
	// gets version file via http
	void version_check(const std::string& version_check_url);
#if 0
	// parsing of Prusaslicer.version2 
	void parse_version_string_old(const std::string& body) const;
#endif
	// parses ini tree of version file, saves to m_online_version_data and queue event(s) to UI 
	void parse_version_string(const std::string& body);
	// thread
	std::thread				m_thread;
	std::atomic_bool        m_cancel;
	std::mutex				m_data_mutex;
	// used to tell if notify user hes about to stop ongoing download
	std::atomic_bool		m_download_ongoing { false };
	bool					get_download_ongoing() const { return m_download_ongoing; }
	// read only variable used to init m_online_version_data.target_path
	boost::filesystem::path m_default_dest_folder; // readonly
	// DownloadAppData read / write needs to be locked by m_data_mutex
	DownloadAppData			m_online_version_data;
	DownloadAppData get_app_data();
	void            set_app_data(DownloadAppData data);
	// set only before version file is downloaded, to keep information to show info dialog about no updates
	// should never change during thread run
	std::atomic_bool		m_triggered_by_user {false};
	bool					get_triggered_by_user() const { return m_triggered_by_user; }
};

AppUpdater::priv::priv() :
	m_cancel (false)
#ifdef __linux__
    , m_default_dest_folder (boost::filesystem::path("/tmp"))
#else
	, m_default_dest_folder (boost::filesystem::path(data_dir()) / "cache")
#endif //_WIN32
{	
	boost::filesystem::path downloads_path = boost::filesystem::path(get_downloads_path());
	if (!downloads_path.empty()) {
		m_default_dest_folder = std::move(downloads_path);
	}
	BOOST_LOG_TRIVIAL(trace) << "App updater default download path: " << m_default_dest_folder;
	
}

bool  AppUpdater::priv::http_get_file(const std::string& url, size_t size_limit, std::function<bool(Http::Progress)> progress_fn, std::function<bool(std::string /*body*/, std::string& error_message)> complete_fn, std::string& error_message) const
{
	bool res = false;
	Http::get(url)
		.size_limit(size_limit)
		.on_progress([&, progress_fn](Http::Progress progress, bool& cancel) {
			// progress function returns true as success (to continue) 
			cancel = (m_cancel ? true : !progress_fn(std::move(progress)));
			if (cancel) {
				// Lets keep error_message empty here - if there is need to show error dialog, the message will be probably shown by whatever caused the cancel.
				//error_message = GUI::format(_u8L("Error getting: `%1%`: Download was canceled."), url);
				BOOST_LOG_TRIVIAL(debug) << "AppUpdater::priv::http_get_file message: "<< error_message;
			}
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			error_message = GUI::format("Error getting: `%1%`: HTTP %2%, %3%",
				url,
				http_status,
				error);
			BOOST_LOG_TRIVIAL(error) << error_message;
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			assert(complete_fn != nullptr);
			res = complete_fn(body, error_message);
		})
		.perform_sync();
	
	return res;
}

boost::filesystem::path AppUpdater::priv::download_file(const DownloadAppData& data) const
{
	boost::filesystem::path dest_path;
	size_t last_gui_progress = 0;
	size_t expected_size = data.size;
	dest_path = data.target_path;
	assert(!dest_path.empty());
	if (dest_path.empty())
	{
		std::string line1 = GUI::format(_u8L("Internal download error for url %1%:"), data.url);
		std::string line2 = _u8L("Destination path is empty.");
		std::string message = GUI::format("%1%\n%2%", line1, line2);
		BOOST_LOG_TRIVIAL(error) << message;
		wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_FAILED);
		evt->SetString(message);
		GUI::wxGetApp().QueueEvent(evt);
		return boost::filesystem::path();
	}

	boost::filesystem::path tmp_path = dest_path;
	tmp_path += format(".%1%%2%", std::to_string(GUI::GLCanvas3D::timestamp_now()), ".download");
	FILE* file;
	file = boost::nowide::fopen(tmp_path.string().c_str(), "wb");
	assert(file != NULL);
	if (file == NULL) {
	    std::string line1 = GUI::format(_u8L("Download from %1% couldn't start:"), data.url);
		std::string line2 = GUI::format(_u8L("Can't create file at %1%"), tmp_path.string());
		std::string message = GUI::format("%1%\n%2%", line1, line2);
		BOOST_LOG_TRIVIAL(error) << message;
		wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_FAILED);
		evt->SetString(message);
		GUI::wxGetApp().QueueEvent(evt);
		return boost::filesystem::path();
	}

	std::string error_message;
	bool res = http_get_file(data.url, 256 * 1024 * 1024  
		// on_progress
		, [&last_gui_progress, expected_size](Http::Progress progress) {
			// size check
			if (progress.dltotal > 0 && progress.dltotal > expected_size) {
				std::string message = GUI::format("Downloading new %1% has failed. The file has incorrect file size. Aborting download.\nExpected size: %2%\nDownload size: %3%", SLIC3R_APP_NAME, expected_size, progress.dltotal);
				BOOST_LOG_TRIVIAL(error) << message;
				wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_FAILED);
				evt->SetString(message);
				GUI::wxGetApp().QueueEvent(evt);
				return false;
			} else if (progress.dltotal > 0 && progress.dltotal < expected_size) { 
				// This is possible error, but we cannot know until the download is finished. Somehow the total size can grow during the download.
				BOOST_LOG_TRIVIAL(info) << GUI::format("Downloading new %1% has incorrect size. The download will continue. \nExpected size: %2%\nDownload size: %3%", SLIC3R_APP_NAME, expected_size, progress.dltotal);
			} 
			// progress event
			size_t gui_progress = progress.dltotal > 0 ? 100 * progress.dlnow / progress.dltotal : 0;
			BOOST_LOG_TRIVIAL(debug) << "App download " << gui_progress << "% " << progress.dlnow << " of " << progress.dltotal;
			if (last_gui_progress < gui_progress && (last_gui_progress != 0 || gui_progress != 100)) {
				last_gui_progress = gui_progress;
				wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_PROGRESS);
				evt->SetString(GUI::from_u8(std::to_string(gui_progress)));
				GUI::wxGetApp().QueueEvent(evt);
			}
			return true;
		}
		// on_complete
		, [&file, dest_path, tmp_path, expected_size](std::string body, std::string& error_message){
			// Size check. Does always 1 char == 1 byte?
			size_t body_size = body.size(); 
			if (body_size != expected_size) {
				error_message = GUI::format(_u8L("Downloaded file has wrong size. Expected size: %1% Downloaded size: %2%"), expected_size, body_size);
				return false;
			}
			if (file == NULL) {
				error_message = GUI::format(_u8L("Can't create file at %1%"), tmp_path.string());
				return false;
			}
			try
			{
				fwrite(body.c_str(), 1, body.size(), file);
				fclose(file);
				boost::filesystem::rename(tmp_path, dest_path);
			}
			catch (const std::exception& e)
			{
				error_message = GUI::format(_u8L("Failed to write to file or to move %1% to %2%:\n%3%"), tmp_path, dest_path, e.what());
				return false;
			}
			return true;
		}
		, error_message
	);
	if (!res)
	{
		if (m_cancel) {
			BOOST_LOG_TRIVIAL(info) << error_message; 
			wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_FAILED); // FAILED with empty msg only closes progress notification
			GUI::wxGetApp().QueueEvent(evt);
		} else {
			std::string message = (error_message.empty() 
				? std::string()
				: GUI::format(_u8L("Downloading new %1% has failed:\n%2%"), SLIC3R_APP_NAME, error_message));
			wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_FAILED);
			if (!message.empty()) {
				BOOST_LOG_TRIVIAL(error) << message;
				evt->SetString(message);
			}
			GUI::wxGetApp().QueueEvent(evt);
		}
		return boost::filesystem::path();
	}
	
	return dest_path;
}

bool AppUpdater::priv::run_downloaded_file(boost::filesystem::path path)
{
	assert(!path.empty());
	return run_file(path);
}

void AppUpdater::priv::version_check(const std::string& version_check_url) 
{
	assert(!version_check_url.empty());
	std::string error_message;
	bool res = http_get_file(version_check_url, 1024
		// on_progress
		, [](Http::Progress progress) { return true; }
		// on_complete
		, [&](std::string body, std::string& error_message) {
			boost::trim(body);
			parse_version_string(body);
			return true;
		}
		, error_message
	);
	if (!res) {
		std::string message = GUI::format("Downloading %1% version file has failed:\n%2%", SLIC3R_APP_NAME, error_message);
		BOOST_LOG_TRIVIAL(error) << message;
		if (m_triggered_by_user) {
			wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_APP_DOWNLOAD_FAILED);
			evt->SetString(message);
			GUI::wxGetApp().QueueEvent(evt);
		}
	}
}

void AppUpdater::priv::parse_version_string(const std::string& body)
{
	size_t start = body.find('[');
	if (start == std::string::npos) {
#if 0
		BOOST_LOG_TRIVIAL(error) << "Could not find property tree in version file. Starting old parsing.";
		parse_version_string_old(body);
		return;
#endif // 0
		BOOST_LOG_TRIVIAL(error) << "Could not find property tree in version file. Checking for application update has failed.";
		// Lets send event with current version, this way if user triggered this check, it will notify him about no new version online.
		std::string version = Semver().to_string();
		wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
		evt->SetString(GUI::from_u8(version));
		GUI::wxGetApp().QueueEvent(evt);
		return;
	}
	std::string tree_string = body.substr(start);
	boost::property_tree::ptree tree;
	std::stringstream ss(tree_string);
	try {
		boost::property_tree::read_ini(ss, tree);
	} catch (const boost::property_tree::ini_parser::ini_parser_error& err) {
		//throw Slic3r::RuntimeError(format("Failed reading version file property tree Error: \"%1%\" at line %2%. \nTree:\n%3%", err.message(), err.line(), tree_string).c_str());
		BOOST_LOG_TRIVIAL(error) << format("Failed reading version file property tree Error: \"%1%\" at line %2%. \nTree:\n%3%", err.message(), err.line(), tree_string);
		return;
	}

	DownloadAppData new_data;

	for (const auto& section : tree) {
		std::string section_name = section.first;

		// online release version info
		if (section_name == 
#ifdef _WIN32
			"release:win64"
#elif __APPLE__
			"release:osx"
#else
			"release:linux"
#endif
			) {
			for (const auto& data : section.second) {
				if (data.first == "url") {
					new_data.url = data.second.data();
					new_data.target_path = m_default_dest_folder / AppUpdater::get_filename_from_url(new_data.url);
					BOOST_LOG_TRIVIAL(info) << format("parsing version string: url: %1%", new_data.url);
				} else if (data.first == "size") {
					new_data.size = std::stoi(data.second.data());
					BOOST_LOG_TRIVIAL(info) << format("parsing version string: expected size: %1%", new_data.size);
				} else if (data.first == "action") {
                    std::string action = data.second.data();
                    if (action == "browser") {
                        new_data.action = AppUpdaterURLAction::AUUA_OPEN_IN_BROWSER;
                    }
                }
			}
		}

		// released versions - to be send to UI layer
		if (section_name == "common") {
			std::vector<std::string> prerelease_versions;
			for (const auto& data : section.second) {
				// release version - save and send to UI layer
				if (data.first == "release") {
					std::string version = data.second.data();
					boost::optional<Semver> release_version = Semver::parse(version);
					if (!release_version) {
						BOOST_LOG_TRIVIAL(error) << format("Received invalid contents from version file: Not a correct semver: `%1%`", version);
						return;
					}
					new_data.version = release_version;
					// Send after all data is read
					/*
					BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
					wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
					evt->SetString(GUI::from_u8(version));
					GUI::wxGetApp().QueueEvent(evt);
					*/
				// prerelease versions - write down to be sorted and send to UI layer
				} else if (data.first == "alpha") {
					prerelease_versions.emplace_back(data.second.data());
				} else if (data.first == "beta") {
					prerelease_versions.emplace_back(data.second.data());
				} else if (data.first == "rc") {
					prerelease_versions.emplace_back(data.second.data());
				}
			}
			// find recent version that is newer than last full release.
			boost::optional<Semver> recent_version;
			std::string				version_string;
			for (const std::string& ver_string : prerelease_versions) {
				boost::optional<Semver> ver = Semver::parse(ver_string);
				if (ver && *new_data.version < *ver && ((recent_version && *recent_version < *ver) || !recent_version)) {
					recent_version = ver;
					version_string = ver_string;
				}
			}
			// send prerelease version to UI layer
			if (recent_version) {
				BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version_string);
				wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE);
				evt->SetString(GUI::from_u8(version_string));
				GUI::wxGetApp().QueueEvent(evt);
			}
		}
	}
	assert(!new_data.url.empty());
	assert(new_data.version);
	// save
	set_app_data(new_data);
	// send
	std::string version = new_data.version.get().to_string();
	BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
	wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
	evt->SetString(GUI::from_u8(version));
	GUI::wxGetApp().QueueEvent(evt);
}

#if 0 //lm:is this meant to be ressurected? //dk: it is code that parses PrusaSlicer.version2 in 2.4.0, It was deleted from PresetUpdater.cpp and I would keep it here for possible reference.
void AppUpdater::priv::parse_version_string_old(const std::string& body) const
{

	// release version
	std::string version;
	const auto first_nl_pos = body.find_first_of("\n\r");
	if (first_nl_pos != std::string::npos)
		version = body.substr(0, first_nl_pos);
	else
		version = body;
	boost::optional<Semver> release_version = Semver::parse(version);
	if (!release_version) {
		BOOST_LOG_TRIVIAL(error) << format("Received invalid contents from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
		return;
	}
	BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
	wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
	evt->SetString(GUI::from_u8(version));
	GUI::wxGetApp().QueueEvent(evt);

	// alpha / beta version
	std::vector<std::string> prerelease_versions;
	size_t nexn_nl_pos = first_nl_pos;
	while (nexn_nl_pos != std::string::npos && body.size() > nexn_nl_pos + 1) {
		const auto last_nl_pos = nexn_nl_pos;
		nexn_nl_pos = body.find_first_of("\n\r", last_nl_pos + 1);
		std::string line;
		if (nexn_nl_pos == std::string::npos)
			line = body.substr(last_nl_pos + 1);
		else
			line = body.substr(last_nl_pos + 1, nexn_nl_pos - last_nl_pos - 1);

		// alpha
		if (line.substr(0, 6) == "alpha=") {
			version = line.substr(6);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for alpha release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
			// beta
		}
		else if (line.substr(0, 5) == "beta=") {
			version = line.substr(5);
			if (!Semver::parse(version)) {
				BOOST_LOG_TRIVIAL(error) << format("Received invalid contents for beta release from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, version);
				return;
			}
			prerelease_versions.emplace_back(version);
		}
	}
	// find recent version that is newer than last full release.
	boost::optional<Semver> recent_version;
	for (const std::string& ver_string : prerelease_versions) {
		boost::optional<Semver> ver = Semver::parse(ver_string);
		if (ver && *release_version < *ver && ((recent_version && *recent_version < *ver) || !recent_version)) {
			recent_version = ver;
			version = ver_string;
		}
	}
	if (recent_version) {
		BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, version);
		wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE);
		evt->SetString(GUI::from_u8(version));
		GUI::wxGetApp().QueueEvent(evt);
	}
}
#endif // 0

DownloadAppData AppUpdater::priv::get_app_data()
{
	const std::lock_guard<std::mutex> lock(m_data_mutex);
	DownloadAppData ret_val(m_online_version_data);
	return ret_val;
}

void AppUpdater::priv::set_app_data(DownloadAppData data)
{
	const std::lock_guard<std::mutex> lock(m_data_mutex);
	m_online_version_data = data;
}

AppUpdater::AppUpdater()
	:p(new priv())
{
}
AppUpdater::~AppUpdater()
{
	if (p && p->m_thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->m_cancel = true;
		p->m_thread.join();
	}
}
void AppUpdater::sync_download()
{
	assert(p);
	// join thread first - it could have been in sync_version
	if (p->m_thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->m_cancel = true;
		p->m_thread.join();
	}
	p->m_cancel = false;

	DownloadAppData input_data = p->get_app_data();
	assert(!input_data.url.empty());

 	p->m_thread = std::thread(
		[this, input_data]() {
			p->m_download_ongoing = true;
			if (boost::filesystem::path dest_path = p->download_file(input_data); boost::filesystem::exists(dest_path)){
				if (input_data.start_after) {
					p->run_downloaded_file(std::move(dest_path));
				} else {
					GUI::desktop_open_folder(dest_path.parent_path());
				}
			}
			p->m_download_ongoing = false;
		});
}

void AppUpdater::sync_version(const std::string& version_check_url, bool from_user)
{
	assert(p);
	// join thread first - it could have been in sync_download
	if (p->m_thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->m_cancel = true;
		p->m_thread.join();
	}
	p->m_triggered_by_user = from_user;
	p->m_cancel = false;
	p->m_thread = std::thread(
		[this, version_check_url]() {
			p->version_check(version_check_url);
		});
}

void AppUpdater::cancel()
{
	p->m_cancel = true;
}
bool AppUpdater::cancel_callback()
{
	cancel();
	return true;
}

std::string AppUpdater::get_default_dest_folder()
{
	return p->m_default_dest_folder.string();
}

std::string AppUpdater::get_filename_from_url(const std::string& url)
{
	size_t slash = url.rfind('/');
	return (slash != std::string::npos ? url.substr(slash + 1) : url);
}

std::string AppUpdater::get_file_extension_from_url(const std::string& url)
{
	size_t dot = url.rfind('.');
	return (dot != std::string::npos ? url.substr(dot) : url);
}

void AppUpdater::set_app_data(DownloadAppData data)
{
	p->set_app_data(std::move(data));
}

DownloadAppData AppUpdater::get_app_data()
{
	return p->get_app_data();
}

bool AppUpdater::get_triggered_by_user() const
{
	return p->get_triggered_by_user();
}

bool AppUpdater::get_download_ongoing() const
{
	return p->get_download_ongoing();
}

} //namespace Slic3r 
