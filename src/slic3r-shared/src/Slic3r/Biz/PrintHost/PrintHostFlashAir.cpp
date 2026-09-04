#include "Slic3r/Biz/PrintHost/PrintHostFlashAir.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "fmt/format.h"
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

namespace {
std::string timestamp_str()
{
    auto t  = std::time(nullptr);
    auto tm = *std::localtime(&t);

    unsigned long fattime = ((tm.tm_year - 80) << 25)
        | ((tm.tm_mon + 1) << 21)
        | (tm.tm_mday << 16)
        | (tm.tm_hour << 11)
        | (tm.tm_min << 5)
        | (tm.tm_sec >> 1);

    return fmt::format("%{:#x}", fattime);
}
} // namespace

bool PrintHostFlashAir::perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    const char* name = get_name();

    const auto upload_filename    = m_upload_data.dest_path.filename();
    const auto upload_parent_path = m_upload_data.dest_path.parent_path();
    std::string test_msg;
    if (!test(test_msg, retry_fn)) {
        error_fn(std::move(test_msg));
        return false;
    }

    bool res = false;

    std::string strDest = upload_parent_path.string();
    if (strDest.front() != '/') // Needs a leading / else root uploads fail.
    {
        strDest.insert(0, "/");
    }

    auto url_prepare = make_url("upload.cgi", "WRITEPROTECT=ON&FTIME", timestamp_str());
    auto url_set_dir = make_url("upload.cgi", "UPDIR", strDest);
    auto url_upload  = make_url("upload.cgi");

    SPDLOG_INFO(
        "{}: Uploading file at %2% / %3%, filename: %4%",
        name,
        url_prepare,
        url_upload,
        upload_filename.string()
    );

    // set filetime for upload and make card write protect to prevent filesystem damage
    std::unique_ptr<Network::IHttp> http_prepare = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url_prepare),
        retry_fn
    );
    http_prepare
        ->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error preparing upload: {}, HTTP {}, body: `{}`", name, error, status, body);
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_complete([&, this](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got prepare result: {}", name, body);
            res = boost::icontains(body, "SUCCESS");
            if (!res) {
                SPDLOG_INFO("{}: Request completed but no SUCCESS message was received.", name);
                error_fn(format_error(body, _u8L("Unknown error occurred"), 0));
            }
        })
        .perform_sync();

    if (!res) {
        return res;
    }

    // start file upload
    std::unique_ptr<Network::IHttp> http_dir = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url_set_dir),
        retry_fn
    );
    http_dir
        ->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error setting upload dir: {}, HTTP {}, body: `{}`", name, error, status, body);
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_complete([&, this](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got dir select result: {}", name, body);
            res = boost::icontains(body, "SUCCESS");
            if (!res) {
                SPDLOG_INFO("{}: Request completed but no SUCCESS message was received.", name);
                error_fn(format_error(body, _u8L("Unknown error occurred"), 0));
            }
        })
        .perform_sync();

    if (!res) {
        return res;
    }

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url_upload),
        retry_fn
    );
    http->form_add_file("file", m_upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            SPDLOG_INFO("{}: File uploaded: HTTP {}: {}", name, status, body);
            res = boost::icontains(body, "SUCCESS");
            if (!res) {
                SPDLOG_INFO("{}: Request completed but no SUCCESS message was received.", name);
                error_fn(format_error(body, _u8L("Unknown error occurred"), 0));
            }
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
                SPDLOG_INFO("{}: Upload canceled", name);
                res = false;
            }
        })
        .perform_sync();

    return res;
}

bool PrintHostFlashAir::test(std::string& msg, RetryFn retry_fn) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    const char* name = get_name();

    bool res = false;
    auto url = make_url("command.cgi", "op", "118");

    SPDLOG_INFO("{}: Get upload enabled at: {}", name, url);

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url),
        retry_fn
    );
    http->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR(
                "{}: Error getting upload enabled: {}, HTTP {}, body: `{}`",
                name,
                error,
                status,
                body
            );
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got upload enabled: {}", name, body);

            res = boost::starts_with(body, "1");
            if (!res) {
                msg = _u8L("Upload not enabled on FlashAir card.");
            }
        })
        .perform_sync();

    return res;
}

std::string PrintHostFlashAir::make_url(const std::string& path) const
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
        if (m_print_host_config.host.back() == '/') {
            return fmt::format("http://{}{}", m_print_host_config.host, path);
        } else {
            return fmt::format("http://{}/{}", m_print_host_config.host, path);
        }
    }
}

std::string PrintHostFlashAir::make_url(
    const std::string& path,
    const std::string& arg,
    const std::string& val
) const
{
    if (m_print_host_config.host.find("http://") == 0
        || m_print_host_config.host.find("https://") == 0)
    {
        if (m_print_host_config.host.back() == '/') {
            return fmt::format("{}{}?{}={}", m_print_host_config.host, path, arg, val);
        } else {
            return fmt::format("{}/{}?{}={}", m_print_host_config.host, path, arg, val);
        }
    } else {
        if (m_print_host_config.host.back() == '/') {
            return fmt::format("http://{}{}?{}={}", m_print_host_config.host, path, arg, val);
        } else {
            return fmt::format("http://{}/{}?{}={}", m_print_host_config.host, path, arg, val);
        }
    }
}

} // namespace Slic3r::Biz::PrintHost
