#include "PrusaConnect.hpp"

#include "Http.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/UserAccount.hpp"
#include "slic3r/GUI/UserAccountUtils.hpp"


#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem.hpp>
#include <curl/curl.h>

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;


namespace Slic3r {
namespace
{

boost::optional<std::string> get_error_message_from_response_body(const std::string& body)
{
    boost::optional<std::string> message;
    std::stringstream ss(body);
    pt::ptree ptree;
    try
    {
        pt::read_json(ss, ptree);
        message = ptree.get_optional<std::string>("message");
    }
    // ignore possible errors if body is not valid JSON
    catch (std::exception&)
    {}

    return message;
}

}

PrusaConnectNew::PrusaConnectNew(DynamicPrintConfig *config) 
    : m_uuid(config->opt_string("print_host"))
    , m_team_id(config->opt_string("printhost_apikey"))
{}

const char* PrusaConnectNew::get_name() const { return "PrusaConnectNew"; }


bool PrusaConnectNew::test(wxString& curl_msg) const
{
    // Test is not used by upload and gets list of files on a device.   
    const std::string name = get_name();
    std::string url = GUI::format("%1%/%2%/files?printer_uuid=%3%", Utils::ServiceConfig::instance().connect_teams_url(), m_team_id, m_uuid);
    const std::string access_token = GUI::wxGetApp().plater()->get_user_account()->get_access_token();
    BOOST_LOG_TRIVIAL(info) << GUI::format("%1%: Get files/raw at: %2%", name, url);
    bool res = true;
  
    auto http = Http::get(std::move(url));
    http.header("Authorization", "Bearer " + access_token);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
        res = false;
        curl_msg = format_error(body, error, status);
    })
    .on_complete([&](std::string body, unsigned) {
         BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Got files/raw: %2%") % name % body;
    })
    .perform_sync();

    return res;
    
}

bool PrusaConnectNew::init_upload(PrintHostUpload upload_data, std::string& out) const
{
    // Register upload. Then upload must be performed immediately with returned "id" 
    bool res = true;
    boost::system::error_code ec;
    boost::uintmax_t size = boost::filesystem::file_size(upload_data.source_path, ec);
    const std::string name = get_name();
    const std::string access_token = GUI::wxGetApp().plater()->get_user_account()->get_access_token();
    const std::string upload_filename = upload_data.upload_path.filename().string();
    std::string url = GUI::format("%1%/app/users/teams/%2%/uploads", get_host(), m_team_id);
    std::string request_body_json = upload_data.data_json;
    
    // replace plaholder filename
    assert(request_body_json.find("%1%") != std::string::npos);
    assert(request_body_json.find("%2%") != std::string::npos);
    request_body_json = GUI::format(request_body_json, upload_filename, size);
    
   
    BOOST_LOG_TRIVIAL(info) << "Register upload to "<< name<<". Url: " << url << "\nBody: " << request_body_json;
    Http http = Http::post(std::move(url));
    http.header("Authorization", "Bearer " + access_token)
        .header("Content-Type", "application/json")
        .set_post_body(request_body_json)
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << boost::format("%1%: File upload registered: HTTP %2%: %3%") % name % status % body;
            out = body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << body;
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error registering file: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            res = false;
            out = get_error_message_from_response_body(body).value_or_eval([&](){
                return GUI::into_u8(format_error(body, error, status));
            });
        })
        .perform_sync();
    return res;
}

bool PrusaConnectNew::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    std::string json = GUI::format(upload_data.data_json, "", "1");
    boost::property_tree::ptree ptree;
    const std::string printer_uuid = GUI::UserAccountUtils::get_keyword_from_json(ptree, json, "printer_uuid");
    wxString printer_page_url = GUI::format("https://connect.prusa3d.com/printer/%1%/dashboard", printer_uuid);
    info_fn(L"prusaconnect_printer_address", printer_page_url);

    std::string init_out;
    if (!init_upload(upload_data, init_out))
    {
        error_fn(GUI::from_u8(init_out));
        return false;
    }
 
    // init reply format: {"id": 1234, "team_id": 12345, "name": "filename.gcode", "size": 123, "hash": "QhE0LD76vihC-F11Jfx9rEqGsk4.", "state": "INITIATED", "source": "CONNECT_USER", "path": "/usb/filename.bgcode"}
    std::string upload_id;
    try
    {
        std::stringstream ss(init_out);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        const auto id_opt = ptree.get_optional<std::string>("id");
        if (!id_opt) {
            error_fn(wxString("Failed to extract upload id from server reply."));
            return false;
        }
        upload_id = *id_opt;
    }
    catch (const std::exception&)
    {
        error_fn(wxString("Failed to extract upload id from server reply."));
        return false;
    }
    const std::string name = get_name();
    const std::string access_token = GUI::wxGetApp().plater()->get_user_account()->get_access_token();
    const std::string url = GUI::format(
        "%1%/app/teams/%2%/files/raw"
        "?upload_id=%3%"
        , get_host(), m_team_id, upload_id);
    bool res = true;


    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Uploading file %2% at %3%, filename: %4%, path: %5%, print: %6%")
        % name
        % upload_data.source_path
        % url
        % upload_data.upload_path.filename().string()
        % upload_data.upload_path.parent_path().string()
        % (upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false");
     
    Http http = Http::put(std::move(url));
    http.set_put_body(upload_data.source_path)
        .header("Content-Type", "text/x.gcode")
        .header("Authorization", "Bearer " + access_token)
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << boost::format("%1%: File uploaded: HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading file: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Http::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                BOOST_LOG_TRIVIAL(info) << name << ": Upload canceled";
                res = false;
            }
        })
        .perform_sync();

    return res;
}

std::string PrusaConnectNew::get_notification_host() const  
{
    return "Prusa Connect"; 
}

bool PrusaConnectNew::get_storage(wxArrayString& storage_path, wxArrayString& storage_name) const
{
    const char* name = get_name();
    bool res = true;
    std::string url = GUI::format("%1%/app/printers/%2%/storages", get_host(), m_uuid);
    const std::string access_token = GUI::wxGetApp().plater()->get_user_account()->get_access_token();
    wxString error_msg;

    struct StorageInfo {
        wxString path;
        wxString name;
        bool read_only = false;
        long long free_space = -1;
    };
    std::vector<StorageInfo> storage;

    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get storage at: %2%") % name % url;

    wxString wlang = GUI::wxGetApp().current_language_code();
    std::string lang = GUI::format(wlang.SubString(0, 1));

    auto http = Http::get(std::move(url));
    http.header("Authorization", "Bearer " + access_token)
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting storage: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            error_msg = L"\n\n" + boost::nowide::widen(error);
            res = false;
            // If status is 0, the communication with the printer has failed completely (most likely a timeout), if the status is <= 400, it is an error returned by the pritner.
            // If 0, we can show error to the user now, as we know the communication has failed. (res = true will do the trick.)
            // if not 0, we must not show error, as not all printers support api/v1/storage endpoint.
            // So we must be extra careful here, or we might be showing errors on perfectly fine communication.
            if (status == 0)
                res = true;

        })
        .on_complete([&](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got storage: %2%") % name % body;
            // {"storages": [{"mountpoint": "/usb", "name": "usb", "free_space": 16340844544, "type": "USB", "is_sfn": true, "read_only": false, "file_count": 1}]}
            try
            {
                std::stringstream ss(body);
                pt::ptree ptree;
                pt::read_json(ss, ptree);

                // what if there is more structure added in the future? Enumerate all elements? 
                if (ptree.front().first != "storages") {
                    res = false;
                    return;
                }
                // each storage has own subtree of storage_list
                for (const auto& section : ptree.front().second) {
                    const auto name = section.second.get_optional<std::string>("name");
                    const auto path = section.second.get_optional<std::string>("mountpoint");
                    const auto space = section.second.get_optional<std::string>("free_space");
                    const auto read_only = section.second.get_optional<bool>("read_only");
                    const auto ro = section.second.get_optional<bool>("ro"); // In PrusaLink 0.7.0RC2 "read_only" value is stored under "ro".
                    const auto available = section.second.get_optional<bool>("available");
                    if (path && (!available || *available)) {
                        StorageInfo si;
                        si.path = boost::nowide::widen(*path);
                        si.name = name ? boost::nowide::widen(*name) : wxString();
                        // If read_only is missing, assume it is NOT read only.
                        // si.read_only = read_only ? *read_only : false; // version without "ro"
                        si.read_only = (read_only ? *read_only : (ro ? *ro : false));
                        si.free_space = space ? std::stoll(*space) : 1;  // If free_space is missing, assume there is free space.
                        storage.emplace_back(std::move(si));
                    }
                }
            }
            catch (const std::exception&)
            {
                res = false;
            }
        })
        .perform_sync();

    for (const auto& si : storage) {
        if (!si.read_only && si.free_space > 0) {
            storage_path.push_back(si.path);
            storage_name.push_back(si.name);
        }
    }
    
    if (res && storage_path.empty()) {
        if (!storage.empty()) { // otherwise error_msg is already filled 
            error_msg = L"\n\n" + _L("Storages found") + L": \n";
            for (const auto& si : storage) {
                error_msg += GUI::format_wxstr(si.read_only ?
                    // TRN %1% = storage path
                    _L("%1% : read only") :
                    // TRN %1% = storage path
                    _L("%1% : no free space"), si.path) + L"\n";
            }
        }
        // TRN %1% = host
        std::string message = GUI::format(_L("Upload has failed. There is no suitable storage found at %1%. "), get_host()) + GUI::into_u8(error_msg);
        BOOST_LOG_TRIVIAL(error) << message;
        throw Slic3r::IOError(message);
    }
    
    return res;
}

wxString PrusaConnectNew::get_test_ok_msg() const
{
    return _L("Test passed.");
}
wxString PrusaConnectNew::get_test_failed_msg(wxString& msg) const
{
    return _L("Test failed.");
}

std::string PrusaConnectNew::get_team_id(const std::string& data) const
{
    boost::property_tree::ptree ptree;
    try {
        std::stringstream ss(data);
        boost::property_tree::read_json(ss, ptree);
    }
    catch (const std::exception&) {
        return {};
    }

    const auto team_id = ptree.get_optional<std::string>("team_id");
    if (team_id)
    {
        return *team_id;
    }
    return {};
}

}
