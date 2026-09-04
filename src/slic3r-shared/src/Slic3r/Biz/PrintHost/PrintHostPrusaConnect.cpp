#include "Slic3r/Biz/PrintHost/PrintHostPrusaConnect.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Log.hpp"

#include "fmt/format.h"
#include <boost/nowide/convert.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include "Slic3r/LegacyFormat.hpp"

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

namespace {

boost::optional<std::string> get_error_message_from_response_body(const std::string& body)
{
    boost::optional<std::string> message;
    try {
        nlohmann::json json = nlohmann::json::parse(body);
        if (json.contains("message") && json["message"].is_string()) {
            message = json["message"].get<std::string>();
        }
    }
    // ignore possible errors if body is not valid JSON
    catch (const nlohmann::json::exception&)
    {}

    return message;
}

// This is not needed anymore, but might come handy when implementing User Account later. If 3.0 is released, this can be removed.
#if 0
std::string parse_json_for_param(const nlohmann::json& json_obj, const std::string& param) {
    if (json_obj.contains(param) && json_obj[param].is_string())
    {
        return json_obj[param];
    }
    for (auto it = json_obj.begin(); it != json_obj.end(); ++it) {
        if (it->is_structured()) {
            if (std::string res = parse_json_for_param(*it, param); !res.empty()) {
                return res;
            }
        }
    }
    return {};
}

std::string get_keyword_from_json(const std::string& body, const std::string& keyword ) 
{
    nlohmann::json json;

    try {
        json = nlohmann::json::parse(body);
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Failed to parse json: {}", e.what());
        SPDLOG_ERROR(json);
        return {};
    }
    return parse_json_for_param(json, keyword);
}
#endif

// Finds exactly 2 placehodlers %1% and %2% and replaces with v1, v2.
std::string replace_placeholders(const std::string& text, const std::string& v1, const std::string& v2)
{
    constexpr std::string_view p1 = "%1%";
    constexpr std::string_view p2 = "%2%";

    ASSERT(!text.empty());

    std::string result;
    result.reserve(text.length());

    size_t current_pos = 0;
    while (current_pos < text.length()) {
        size_t p1_pos               = text.find(p1, current_pos);
        size_t p2_pos               = text.find(p2, current_pos);
        size_t next_placeholder_pos = std::min(p1_pos, p2_pos);
        if (next_placeholder_pos == std::string::npos) {
            result.append(text, current_pos, text.length() - current_pos);
            break;
        }
        result.append(text, current_pos, next_placeholder_pos - current_pos);
        if (next_placeholder_pos == p1_pos) {
            result.append(v1);
            current_pos = next_placeholder_pos + p1.length();
        } else {
            result.append(v2);
            current_pos = next_placeholder_pos + p2.length();
        }
    }
    return result;
}
} // namespace

bool PrintHostPrusaConnect::test(std::string& curl_msg, RetryFn retry_fn) const
{
    // Test is not used by upload and gets list of files on a device.

    const PhysicalPrinter::ConnectUpload* auth = std::get_if<PhysicalPrinter::ConnectUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    const std::string name = get_name();
    std::string url        = fmt::format(
        "{}/{}/files?printer_uuid={}",
        Network::ServiceConfig::instance().connect_teams_url(),
        auth->team_id,
        auth->printer_uuid
    );
    SPDLOG_INFO("{}: Get files/raw at: {}", name, url);
    bool res = true;

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url),
        retry_fn
    );
    http->header("Authorization", "Bearer " + auth->access_token);
    http->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error getting version: {}, HTTP {}, body: `{}`", name, error, status, body);
            res      = false;
            curl_msg = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got files/raw: {}", name, body);
        })
        .perform_sync();

    return res;
}

bool PrintHostPrusaConnect::init_upload(
    const PrintHostJobData& upload_data,
    std::string& out,
    RetryFn retry_fn
) const
{
    // Register upload. Then upload must be performed immediately with returned "id"

    const PhysicalPrinter::ConnectUpload* auth = std::get_if<PhysicalPrinter::ConnectUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    bool res = true;
    boost::system::error_code ec;
    boost::uintmax_t size = boost::filesystem::file_size(upload_data.source_path, ec);
    if (ec) {
        SPDLOG_ERROR("Failed to read file size of {}", upload_data.source_path.string());
        return false;
    }
    const std::string name            = get_name();
    const std::string upload_filename = upload_data.dest_path.filename().string();
    std::string url                   = fmt::format(
        "{}/app/users/teams/{}/uploads",
        m_print_host_config.host,
        auth->team_id
    );

    std::string request_body_json;
    try {
        nlohmann::json j  = nlohmann::json::parse(upload_data.request_body_json);
        j["filename"]     = upload_filename;
        j["size"]         = size;
        request_body_json = j.dump();
    } catch (const nlohmann::json::parse_error& e) {
        SPDLOG_ERROR("Could not parse request_body_json: {}", e.what());
        return false;
    }

    SPDLOG_INFO("Register upload to " + name + ". Url: " + url + "\nBody: " + request_body_json);
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
    http->header("Authorization", "Bearer " + auth->access_token)
        .header("Content-Type", "application/json")
        .set_post_body(std::move(request_body_json))
        .on_complete([&](std::string body, unsigned status) {
            SPDLOG_INFO("{}: File upload registered: HTTP {}: {}", name, status, body);
            out = body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error registering file: {}, HTTP {}, body: `{}`", name, error, status, body);
            res = false;
            out = get_error_message_from_response_body(body).value_or_eval([&]() {
                return format_error(body, error, status);
            });
        })
        .perform_sync();
    return res;
}

bool PrintHostPrusaConnect::perform(
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn
) const
{
     const PhysicalPrinter::ConnectUpload* auth = std::get_if<PhysicalPrinter::ConnectUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    std::string printer_page_url = fmt::format(
        "{}/printer/{}/dashboard",
        Network::ServiceConfig::instance().connect_url(),
        auth->printer_uuid
    );
    //info_fn(PrintHostJobInfoTag::ConnectPrinterAddress, printer_page_url);

    std::string init_out;
    if (!init_upload(m_upload_data, init_out, retry_fn)) {
        error_fn(init_out);
        return false;
    }

    // init reply format: {"id": 1234, "team_id": 12345, "name": "filename.gcode", "size": 123, "hash": "QhE0LD76vihC-F11Jfx9rEqGsk4.", "state": "INITIATED", "source": "CONNECT_USER", "path": "/usb/filename.bgcode"}
    size_t upload_id;
    try {
        nlohmann::json json = nlohmann::json::parse(init_out);
        if (!json.contains("id") || !json["id"].is_number_unsigned()) {
            error_fn(_u8L("Failed to extract upload id from server reply."));
            return false;
        }
        upload_id = json["id"].get<std::size_t>();
    } catch (const nlohmann::json::exception&) {
        error_fn(_u8L("Failed to extract upload id from server reply."));
        return false;
    }
    const std::string name = get_name();
    std::string url        = fmt::format(
        "{}/app/teams/{}/files/raw"
               "?upload_id={}",
        Network::ServiceConfig::instance().connect_url(),
        auth->team_id,
        upload_id
    );
    bool res = true;

    SPDLOG_INFO(
        "{}: Uploading file at {}, filename: {}, path: {}, print: {}",
        name,
        url,
        m_upload_data.dest_path.filename().string(),
        m_upload_data.dest_path.parent_path().string(),
        (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false")
    );

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Put,
        std::move(url),
        retry_fn
    );
    http->set_put_body(m_upload_data.source_path)
        .header("Content-Type", "text/x.gcode")
        .header("Authorization", "Bearer " + auth->access_token)
        .on_complete([&](std::string body, unsigned status) {
            SPDLOG_INFO("{}: File uploaded: HTTP {}: {}", name, status, body);
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error uploading file: {}, HTTP {}, body: `{}`", name, error, status, body);
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Network::IHttp::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                SPDLOG_INFO("PrusaConnect: Upload canceled");
                res = false;
            }
        })
        .perform_sync();

    return res;
}

} // namespace Slic3r::Biz::PrintHost
