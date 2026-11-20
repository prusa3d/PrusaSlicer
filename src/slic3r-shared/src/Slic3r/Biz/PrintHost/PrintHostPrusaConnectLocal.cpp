#include "Slic3r/Biz/PrintHost/PrintHostPrusaConnectLocal.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <fmt/format.h>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>

#include <optional>

namespace Slic3r::Biz::PrintHost {

namespace {

std::optional<std::string> error_message_of(const std::string& body)
{
    try {
        const nlohmann::json json = nlohmann::json::parse(body);
        if (auto it = json.find("message"); it != json.end() && it->is_string()) {
            return it->get<std::string>();
        }
    } catch (const nlohmann::json::exception&) {
    }
    return std::nullopt;
}

std::optional<std::size_t> upload_id_of(const std::string& body)
{
    try {
        const nlohmann::json json = nlohmann::json::parse(body);
        if (auto it = json.find("id"); it != json.end() && it->is_number_unsigned()) {
            return it->get<std::size_t>();
        }
    } catch (const nlohmann::json::exception&) {
    }
    return std::nullopt;
}

} // namespace

bool PrintHostPrusaConnectLocal::test(std::string& msg, ProgressFn progress_fn, RetryFn retry_fn) const
{
    const PhysicalPrinter::ConnectUpload* auth = std::get_if<PhysicalPrinter::ConnectUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    const std::string name = get_name();
    std::string url        = Network::ServiceConfig::instance().connect_printer_list_url();
    bool res               = true;

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url),
        retry_fn
    );
    http->header("Authorization", "Bearer " + auth->access_token)
        .on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error listing printers: {}, HTTP {}, body: `{}`", name, error, status, body);
            auth->report_unauthorized(status, auth->access_token);
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&](std::string, unsigned status) {
            SPDLOG_INFO("{}: Printer list: HTTP {}", name, status);
        })
        .on_progress(progress_fn)
        .perform_sync();

    return res;
}

bool PrintHostPrusaConnectLocal::register_upload(
    const PhysicalPrinter::ConnectUpload& auth,
    RetryFn retry_fn,
    ErrorFn error_fn,
    std::size_t& upload_id
) const
{
    boost::system::error_code ec;
    const boost::uintmax_t size = boost::filesystem::file_size(m_upload_data.source_path, ec);
    if (ec) {
        SPDLOG_ERROR("Failed to read file size of {}", m_upload_data.source_path.string());
        error_fn(_u8L("Failed to read the file to upload."));
        return false;
    }

    std::string request_body_json;
    try {
        nlohmann::json json = nlohmann::json::parse(m_upload_data.request_body_json);
        json["filename"]    = m_upload_data.dest_path.filename().string();
        json["size"]        = size;
        request_body_json   = json.dump();
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse request_body_json: {}", e.what());
        error_fn(_u8L("Prusa Connect sent an invalid print request."));
        return false;
    }

    const std::string name = get_name();
    std::string url = fmt::format("{}/app/users/teams/{}/uploads", m_print_host_config.host, auth.team_id);
    SPDLOG_INFO("{}: Register upload at {}\nBody: {}", name, url, request_body_json);

    bool res = true;
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
    http->header("Authorization", "Bearer " + auth.access_token)
        .header("Content-Type", "application/json")
        .set_post_body(std::move(request_body_json))
        .on_complete([&](std::string body, unsigned status) {
            SPDLOG_INFO("{}: File upload registered: HTTP {}: {}", name, status, body);
            if (const std::optional<std::size_t> id = upload_id_of(body)) {
                upload_id = *id;
            } else {
                error_fn(_u8L("Failed to extract upload id from server reply."));
                res = false;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error registering file: {}, HTTP {}, body: `{}`", name, error, status, body);
            auth.report_unauthorized(status, auth.access_token);
            error_fn(error_message_of(body).value_or(format_error(body, error, status)));
            res = false;
        })
        .perform_sync();
    return res;
}

bool PrintHostPrusaConnectLocal::upload_file(
    const PhysicalPrinter::ConnectUpload& auth,
    std::size_t upload_id,
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn
) const
{
    const std::string name = get_name();
    std::string url        = fmt::format(
        "{}/app/teams/{}/files/raw?upload_id={}",
        m_print_host_config.host,
        auth.team_id,
        upload_id
    );
    SPDLOG_INFO(
        "{}: Uploading file at {}, filename: {}, path: {}, print: {}",
        name,
        url,
        m_upload_data.dest_path.filename().string(),
        m_upload_data.dest_path.parent_path().string(),
        m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false"
    );

    bool res = true;
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Put,
        std::move(url),
        retry_fn
    );
    http->set_put_body(m_upload_data.source_path)
        .header("Content-Type", "text/x.gcode")
        .header("Authorization", "Bearer " + auth.access_token)
        .on_complete([&](std::string body, unsigned status) {
            SPDLOG_INFO("{}: File uploaded: HTTP {}: {}", name, status, body);
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error uploading file: {}, HTTP {}, body: `{}`", name, error, status, body);
            auth.report_unauthorized(status, auth.access_token);
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Network::IHttp::Progress progress, bool& cancel) {
            if (progress_fn) {
                progress_fn(std::move(progress), cancel);
            }
            if (cancel) {
                SPDLOG_INFO("{}: Upload canceled", name);
                res = false;
            }
        })
        .perform_sync();

    return res;
}

bool PrintHostPrusaConnectLocal::perform(
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn
) const
{
    const PhysicalPrinter::ConnectUpload* auth = std::get_if<PhysicalPrinter::ConnectUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    if (auth->team_id.empty()) {
        SPDLOG_ERROR("{}: No team id for printer {}", get_name(), auth->printer_uuid);
        error_fn(_u8L("Prusa Connect did not provide the team of the selected printer."));
        return false;
    }

    if (!auth->printer_page_url.empty()) {
        info_fn(PrintHostJobInfoTag::ConnectPrinterAddress, auth->printer_page_url);
    }

    std::size_t upload_id = 0;
    if (!register_upload(*auth, retry_fn, error_fn, upload_id)) {
        return false;
    }
    return upload_file(*auth, upload_id, progress_fn, retry_fn, error_fn);
}

} // namespace Slic3r::Biz::PrintHost
