#include "Slic3r/Biz/PrintHost/PrintHostRepetier.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

namespace {
bool validate_repetier(const boost::optional<std::string>& name, const boost::optional<std::string>& soft)
{
    if (soft) {
        // See https://github.com/prusa3d/PrusaSlicer/issues/7807:
        // Repetier allows "rebranding", so the "name" value is not reliable when detecting
        // server type. Newer Repetier versions send "software", which should be invariant.
        return ((*soft) == "Repetier-Server");
    } else {
        // If there is no "software" value, validate as we did before:
        return name ? boost::starts_with(*name, "Repetier") : true;
    }
}
} // namespace

bool PrintHostRepetier::perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    const char* name = get_name();

    const auto upload_filename    = m_upload_data.dest_path.filename();
    const auto upload_parent_path = m_upload_data.dest_path.parent_path();

    std::string test_msg;
    if (!test(test_msg, retry_fn)) {
        error_fn(std::move(test_msg));
        return false;
    }

    bool res = true;

    auto url = m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ?
        make_url(fmt::format("printer/job/{}", auth->port)) :
        make_url(fmt::format("printer/model/{}", auth->port));

    SPDLOG_INFO(
        fmt::format(
            "{}: Uploading file at {}, filename: {}, path: {}, print: {}, group: {}",
            name,
            url,
            upload_filename.string(),
            upload_parent_path.string(),
            (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false"),
            m_upload_data.group
        )
    );

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
    set_auth(http.get());

    if (!m_upload_data.group.empty() && m_upload_data.group != _u8L("Default")) {
        http->form_add("group", m_upload_data.group);
    }

    if (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint) {
        http->form_add("name", upload_filename.string());
        http->form_add(
            "autostart",
            "true"
        ); // See https://github.com/prusa3d/PrusaSlicer/issues/7807#issuecomment-1235519371
    }

    http->form_add("a", "upload")
        .form_add_file("filename", m_upload_data.source_path, upload_filename.string())
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
                SPDLOG_INFO("Repetier: Upload canceled");
                res = false;
            }
        })
        .perform_sync();

    return res;
}

bool PrintHostRepetier::test(std::string& msg, RetryFn retry_fn) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    const char* name = get_name();

    bool res = true;
    auto url = make_url("printer/info");

    SPDLOG_INFO("{}: List version at: {}", name, url);

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url),
        retry_fn
    );
    set_auth(http.get());

    http->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error getting version: {}, HTTP {}, body: `{}`", name, error, status, body);
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got version: {}", name, body);

            try {
                nlohmann::json json = nlohmann::json::parse(body);
                boost::optional<std::string> text;
                if (json.contains("name") && json["name"].is_string()) {
                    text = json["name"].get<std::string>();
                }
                boost::optional<std::string> soft;
                if (json.contains("software") && json["software"].is_string()) {
                    soft = json["software"].get<std::string>();
                }
                res = validate_repetier(text, soft);
                if (!res) {
                    msg = fmt::format(
                        "{} {}",
                        _u8L("Mismatched type of print host:"),
                        (soft ? *soft : (text ? *text : "Repetier"))
                    );
                }
            } catch (const nlohmann::json::exception&) {
                res = false;
                msg = "Could not parse server response";
            }
        })
        .perform_sync();

    return res;
}

std::string PrintHostRepetier::make_url(const std::string& path) const
{
    if (m_print_host_config.host.find("http://") == 0
        || m_print_host_config.host.find("https://") == 0)
    {
        if (m_print_host_config.host.back() == '/') {
            return fmt::format("{}{}", m_print_host_config.host, path);
        } else {
            return fmt::format("{}/{}", m_print_host_config.host, path);
        }
    } else {
        return fmt::format("http://{}/{}", m_print_host_config.host, path);
    }
}

void PrintHostRepetier::set_auth(Network::IHttp* http) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    http->header("X-Api-Key", auth->api_key);

    if (!auth->ca_file.empty()) {
        http->ca_file(auth->ca_file);
    }
}

} // namespace Slic3r::Biz::PrintHost
