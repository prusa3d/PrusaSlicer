///|/ Copyright (c) Prusa Research 2023 David Kocík @kocikdav
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DownloaderFileGet_hpp_
#define slic3r_DownloaderFileGet_hpp_

#include "Event.hpp"

#include "../Utils/Http.hpp"

#include <memory>
#include <string>
#include <wx/event.h>
#include <wx/frame.h>
#include <boost/filesystem.hpp>

namespace Slic3r {
namespace GUI {
class FileGet : public std::enable_shared_from_this<FileGet> {
private:
	struct priv;
public:
	FileGet(int ID, std::string url, const std::string& filename, wxEvtHandler* evt_handler,const boost::filesystem::path& dest_folder, bool load_after);
	FileGet(FileGet&& other);
	~FileGet();

	void get();
	void cancel();
	void pause();
	void resume();
	static bool is_subdomain(const std::string& url, const std::string& domain);
	// Turns an untrusted name (from URL or from HTTP headers) into a plain file name without any path components.
	// Returns an empty string if no usable name remains.
	static std::string sanitize_filename(const std::string& filename);
private:
	std::unique_ptr<priv> p;
};
struct DownloadEventData
{
    int id;
    wxString path;
    bool load_after;
};
// int = DOWNLOAD ID; string = file path
wxDECLARE_EVENT(EVT_DWNLDR_FILE_COMPLETE, Event<DownloadEventData>);
// int = DOWNLOAD ID; string = error msg
wxDECLARE_EVENT(EVT_DWNLDR_FILE_PROGRESS, wxCommandEvent);
// int = DOWNLOAD ID; string = progress percent
wxDECLARE_EVENT(EVT_DWNLDR_FILE_ERROR, wxCommandEvent);
// int = DOWNLOAD ID; string = name
wxDECLARE_EVENT(EVT_DWNLDR_FILE_NAME_CHANGE, wxCommandEvent);
// int = DOWNLOAD ID;
wxDECLARE_EVENT(EVT_DWNLDR_FILE_PAUSED, wxCommandEvent);
// int = DOWNLOAD ID;
wxDECLARE_EVENT(EVT_DWNLDR_FILE_CANCELED, wxCommandEvent);
}
}
#endif
