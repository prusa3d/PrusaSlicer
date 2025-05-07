///|/ Copyright (c) Prusa Research 2022 David Kocík @kocikdav
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_AppUpdate_hpp_
#define slic3r_AppUpdate_hpp_

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include "libslic3r/Utils.hpp"
#include "wx/event.h"

//class boost::filesystem::path;

namespace Slic3r {

#ifdef __APPLE__
// implmented at MacUtils.mm
std::string get_downloads_path_mac();
#endif //__APPLE__

enum class AppUpdaterURLAction {
    AUUA_DOWNLOAD,
    AUUA_OPEN_IN_BROWSER
};
struct DownloadAppData
{
	std::string				url;
	bool					start_after;
	boost::optional<Semver> version;
	size_t				    size;
	boost::filesystem::path target_path;
    AppUpdaterURLAction     action { AppUpdaterURLAction::AUUA_DOWNLOAD };
};

class AppUpdater
{
public:
	AppUpdater();
	~AppUpdater();
	AppUpdater(AppUpdater&&) = delete;
	AppUpdater(const AppUpdater&) = delete;
	AppUpdater& operator=(AppUpdater&&) = delete;
	AppUpdater& operator=(const AppUpdater&) = delete;

	// downloads app file
	void sync_download();
	// downloads version file
	void sync_version(const std::string& version_check_url, bool from_user);
	void cancel();
	bool cancel_callback();

	std::string get_default_dest_folder();

	static std::string get_filename_from_url(const std::string& url);
	static std::string get_file_extension_from_url(const std::string& url);

	// atomic bool
	bool				get_triggered_by_user() const;
	bool				get_download_ongoing() const;
	// mutex access
	void				set_app_data(DownloadAppData data);
	DownloadAppData		get_app_data();
private:
	struct priv;
	std::unique_ptr<priv> p;
};

wxDECLARE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);
wxDECLARE_EVENT(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, wxCommandEvent);
wxDECLARE_EVENT(EVT_SLIC3R_APP_DOWNLOAD_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(EVT_SLIC3R_APP_DOWNLOAD_FAILED, wxCommandEvent);
wxDECLARE_EVENT(EVT_SLIC3R_APP_OPEN_FAILED, wxCommandEvent);
} //namespace Slic3r 
#endif
