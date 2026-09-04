#include "Slic3r/Biz/PrintHost/PrintHostAstroBox.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

bool PrintHostAstroBox::perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    const char* name = get_name();

    const auto upload_filename    = m_upload_data.dest_path.filename();
    const auto upload_parent_path = m_upload_data.dest_path.parent_path();

    std::string test_msg;
    if (!test(test_msg, retry_fn)) {
        error_fn(std::move(test_msg));
        return false;
    }

    bool res = true;

    auto url = make_url("api/files/local");

    SPDLOG_INFO(
        "{}: Uploading file {} at {}, filename: {}, path: {}, print: {}",
        name,
        m_upload_data.dest_path.string(),
        url,
        upload_filename.string(),
        upload_parent_path.string(),
        (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false")
    );

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
    set_auth(http.get());
    http->form_add(
            "print",
            m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false"
    )
        .form_add("path", upload_parent_path.string()) // XXX: slashes on windows ???
        .form_add_file("file", m_upload_data.source_path, upload_filename.string())
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
                SPDLOG_INFO("AstroBox: Upload canceled");
                res = false;
            }
        })
        .perform_sync();

    return res;
}

bool PrintHostAstroBox::test(std::string& msg, RetryFn retry_fn) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    const char* name = get_name();

    bool res = true;
    auto url = make_url("api/version");

    SPDLOG_INFO("{}: Get version at: {}", name, url);

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
        .on_complete([&, this](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got version: {}", name, body);
            try {
                nlohmann::json json = nlohmann::json::parse(body);
                if (!json.contains("api") || !json["api"].is_string()) {
                    res = false;
                    return;
                }
                boost::optional<std::string> text;
                if (json.contains("text") && json["text"].is_string()) {
                    text = json["text"].get<std::string>();
                }
                res = validate_version_text(text);
                if (!res) {
                    msg = fmt::format(
                        "{} {}",
                        _u8L("Mismatched type of print host:"),
                        (text ? *text : "AstroBox")
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

std::string PrintHostAstroBox::make_url(const std::string& path) const
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

void PrintHostAstroBox::set_auth(Network::IHttp* http) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    http->header("X-Api-Key", auth->api_key);

    if (!auth->ca_file.empty()) {
        http->ca_file(auth->ca_file);
    }
}

bool PrintHostAstroBox::validate_version_text(const boost::optional<std::string>& version_text) const
{
    return version_text ? boost::starts_with(*version_text, "AstroBox") : true;
}

} // namespace Slic3r::Biz::PrintHost
