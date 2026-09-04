#include "Slic3r/Biz/PrintHost/PrintHostPrusaLink.hpp"

#include "Slic3r/Biz/Network/Bonjour.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Log.hpp"

#include "fmt/format.h"
#include <boost/filesystem.hpp>
#include <boost/system/error_code.hpp>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>
#include <utility>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

PrintHostPrusaLink::PrintHostPrusaLink(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) :
    IPrintHost(std::move(config), std::move(data))
{}

bool PrintHostPrusaLink::validate_version_text(const boost::optional<std::string>& version_text) const
{
    return version_text ? (boost::starts_with(*version_text, "PrusaLink")
                           || boost::starts_with(*version_text, "OctoPrint")) :
                          false;
}

void PrintHostPrusaLink::set_auth(Network::IHttp* http) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    switch (auth->auth_type) {
    case Domain::PrintHostAuthType::ApiKey:
        http->header("X-Api-Key", auth->api_key);
        break;
    case Domain::PrintHostAuthType::Digest:
        http->auth_digest(auth->username, auth->password);
        break;
    default:
        ASSERT(false, "PrusaLink does not support other auth method than api key or http digest.");
        break;
    }

    if (!auth->ca_file.empty()) {
        http->ca_file(auth->ca_file);
    }
}

std::string PrintHostPrusaLink::make_url(const std::string& path) const
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

bool PrintHostPrusaLink::perform(ProgressFn progress_fn, RetryFn retry_fn, ErrorFn error_fn, InfoFn info_fn) const
{
#ifndef WIN32
    return upload_inner_with_host(progress_fn, retry_fn, error_fn, info_fn);
#else
    std::string host = Network::IHttp::extract_host_from_url(m_print_host_config.host);

    // decide what to do based on host - resolve hostname or upload to ip
    std::vector<boost::asio::ip::address> resolved_addr;
    boost::system::error_code ec;
    boost::asio::ip::address host_ip = boost::asio::ip::make_address(host, ec);
    if (!ec) {
        resolved_addr.push_back(host_ip);
    } else if (boost::algorithm::ends_with(host, ".local")) {
        Bonjour("octoprint")
            .set_hostname(host)
            .set_retries(5) // number of rounds of queries send
            .set_timeout(1) // after each timeout, if there is any answer, the resolving will stop
            .on_resolve([&ra = resolved_addr](const std::vector<BonjourReply>& replies) {
                for (const auto& rpl : replies) {
                    boost::asio::ip::address ip(rpl.ip);
                    ra.emplace_back(ip);
                    SPDLOG_INFO("Resolved IP address: {}", rpl.ip.to_string());
                }
            })
            .resolve_sync();
    }

    if (resolved_addr.empty()) {
        // no resolved addresses - try system resolving
        SPDLOG_ERROR(
            "Failed to resolve hostname {} into the IP address. Starting upload with system resolving.",
            m_print_host_config.host
        );
        return upload_inner_with_host(progress_fn, retry_fn, error_fn, info_fn);
    } else if (resolved_addr.size() == 1) {
        // one address resolved - upload there
        return upload_inner_with_resolved_ip(
            progress_fn,
            retry_fn,
            error_fn,
            info_fn,
            resolved_addr.front()
        );
    } else if (resolved_addr.size() == 2 && resolved_addr[0].is_v4() != resolved_addr[1].is_v4()) {
        // there are just 2 addresses and 1 is ip_v4 and other is ip_v6
        // try sending to both. (Then if both fail, show both error msg after second try)
        std::string error_message;
        if (!upload_inner_with_resolved_ip(
                progress_fn,
                retry_fn,
                [&msg = error_message, resolved_addr](std::string error) {
                    msg = fmt::format("{}: {}", resolved_addr.front().to_string(), error);
                },
                info_fn,
                resolved_addr.front()
            )
            && !upload_inner_with_resolved_ip(
                progress_fn,
                retry_fn,
                [&msg = error_message, resolved_addr](std::string error) {
                    msg += fmt::format("\n{}: {}", resolved_addr.back().to_string(), error);
                },
                info_fn,
                resolved_addr.back()
            ))
        {
            error_fn(error_message);
            return false;
        }
        return true;
    } else {
        // There are multiple addresses - user needs to choose which to use. (Here used to be dialog (We are in worker thread!!))
        // Lets try all now until some works?
        for (size_t i = 0; i < resolved_addr.size(); i++) {
            if (upload_inner_with_resolved_ip(progress_fn, retry_fn, error_fn, info_fn, resolved_addr[i]))
            {
                return true;
            }
        }
    }
    return false;
#endif // WIN32
}

bool PrintHostPrusaLink::test(std::string& msg, RetryFn retry_fn) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

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
                        (text ? *text : name)
                    );
                }
            } catch (const nlohmann::json::exception&) {
                res = false;
                msg = "Could not parse server response";
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = address;
        })
#endif // WIN32
        .perform_sync();

    return res;
}

bool PrintHostPrusaLink::test_with_method_check(std::string& msg, bool& use_put, RetryFn retry_fn) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    const char* name = get_name();

    bool res = true;
    auto url = make_url("api/version");

    SPDLOG_INFO("{}: Get version at: {}", name, url);
    // Here we do not have to add custom "Host" header - the url contains host filled by user and libCurl will set the header by itself.
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
                        (text ? *text : "OctoPrint")
                    );
                    use_put = false;
                    return;
                }
                if (json.contains("capabilities") && json["capabilities"].is_structured()) {
                    if (json["capabilities"].contains("upload-by-put")
                        && json["capabilities"]["upload-by-put"].is_boolean())
                    {
                        use_put = json["capabilities"]["upload-by-put"].get<bool>();
                    }
                }
            } catch (const nlohmann::json::exception&) {
                res = false;
                msg = "Could not parse server response";
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = address;
        })
#endif // WIN32
        .perform_sync();

    return res;
}

#ifdef WIN32
bool PrintHostPrusaLink::test_with_resolved_ip_and_method_check(
    std::string& msg,
    bool& use_put,
    RetryFn retry_fn
) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    const char* name = get_name();
    bool res         = true;
    // Msg contains ip string.
    std::string url = Network::IHttp::substitute_host(make_url("api/version"), msg);
    msg.clear();

    SPDLOG_INFO("{}: Get version at: {}", name, url);

    std::string host = Network::IHttp::extract_host_from_url(m_print_host_config.host);
    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        url,
        retry_fn
    );
    // "Host" header is necessary here. We have resolved IP address and substituted it into "url" variable.
    // And when creating Http object above, libcurl automatically includes "Host" header from address it got.
    // Thus "Host" is set to the resolved IP instead of host filled by user. We need to change it back.
    // Not changing the host would work on the most cases (where there is 1 service on 1 hostname) but would break when f.e. reverse proxy is used (issue #9734).
    // Also when allow_ip_resolve = 0, this is not needed, but it should not break anything if it stays.
    // https://www.rfc-editor.org/rfc/rfc7230#section-5.4
    http->header("Host", host);
    set_auth(http.get());
    http->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR(
                "{}: Error getting version at {} : {}, HTTP {}, body: `{}`",
                name,
                url,
                error,
                status,
                body
            );
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
                        (text ? *text : "OctoPrint")
                    );
                    use_put = false;
                    return;
                }
                if (json.contains("capabilities") && json["capabilities"].is_structured()) {
                    if (json["capabilities"].contains("upload-by-put")
                        && json["capabilities"]["upload-by-put"].is_boolean())
                    {
                        use_put = json["capabilities"]["upload-by-put"].get<bool>();
                    }
                }
            } catch (const nlohmann::json::exception&) {
                res = false;
                msg = "Could not parse server response";
            }
        })
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
        .perform_sync();

    return res;
}

bool PrintHostPrusaLink::upload_inner_with_resolved_ip(
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn,
    const boost::asio::ip::address& resolved_addr
) const
{
    info_fn(PrintHostJobInfoTag::Resolve, (resolved_addr.to_string()));

    // If test fails, test_msg contains the error message.
    // Otherwise on Windows it contains the resolved IP address of the host.
    // Test_msg already contains resolved ip and will be cleared on start of test().
    std::string test_msg = resolved_addr.to_string();
    bool use_put         = false;
    if (!test_with_resolved_ip_and_method_check(test_msg, use_put, retry_fn)) {
        error_fn(std::move(test_msg));
        return false;
    }

    const char* name                  = get_name();
    const fs::path upload_filename    = m_upload_data.dest_path.filename();
    const fs::path upload_parent_path = m_upload_data.dest_path.parent_path();
    std::string storage_path          = (use_put ? "api/v1/files" : "api/files");
    storage_path += (m_upload_data.storage.empty() ? "/local" : m_upload_data.storage);
    std::string url = Network::IHttp::substitute_host(make_url(storage_path), resolved_addr.to_string());
    bool result = true;
    info_fn(PrintHostJobInfoTag::Resolve, url);

    SPDLOG_INFO(
        "{}: Uploading file {} at {}, filename: {}, path: {}, print: {}, method: {}",
        name,
        m_upload_data.dest_path.string(),
        url,
        upload_filename.string(),
        upload_parent_path.string(),
        (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false"),
        (use_put ? "PUT" : "POST")
    );

    if (use_put)
        return put_inner(std::move(url), name, progress_fn, retry_fn, error_fn, info_fn);
    return post_inner(std::move(url), name, progress_fn, retry_fn, error_fn, info_fn);
}

#endif // WIN32

bool PrintHostPrusaLink::upload_inner_with_host(
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn
) const
{
    const char* name = get_name();

    const auto upload_filename    = m_upload_data.dest_path.filename();
    const auto upload_parent_path = m_upload_data.dest_path.parent_path();

    // If test fails, test_msg contains the error message.
    // Otherwise on Windows it contains the resolved IP address of the host.
    std::string test_msg;
    bool use_put = false;
    if (!test_with_method_check(test_msg, use_put, retry_fn)) {
        error_fn(std::move(test_msg));
        return false;
    }

    std::string url;
    std::string storage_path = (use_put ? "api/v1/files" : "api/files");
    storage_path += (m_upload_data.storage.empty() ? "/local" : m_upload_data.storage);
#ifdef WIN32
    // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
    if (m_print_host_config.host.find("https://") == 0 || test_msg.empty())
#endif // _WIN32
    {
        // If https is entered we assume signed certificate is being used
        // IP resolving will not happen - it could resolve into address not being specified in cert
        url = make_url(storage_path);
    }
#ifdef WIN32
    else
    {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Curl uses easy_getinfo to get ip address of last successful transaction.
        // If it got the address use it instead of the stored in "host" variable.
        // This new address returns in "test_msg" variable.
        // Solves troubles of uploades failing with name address.
        // in original address (m_host) replace host for resolved ip
        info_fn(PrintHostJobInfoTag::Resolve, test_msg);
        url = Network::IHttp::substitute_host(make_url(storage_path), test_msg);
        SPDLOG_INFO("Upload address after ip resolve: {}", url);
    }
#endif // _WIN32
    SPDLOG_INFO(
        "{}: Uploading file {} at {}, filename: {}, path: {}, print: {}, method: {}",
        name,
        m_upload_data.dest_path.string(),
        url,
        upload_filename.string(),
        upload_parent_path.string(),
        (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint ? "true" : "false"),
        (use_put ? "PUT" : "POST")
    );

    if (use_put)
        return put_inner(std::move(url), name, progress_fn, retry_fn, error_fn, info_fn);
    return post_inner(std::move(url), name, progress_fn, retry_fn, error_fn, info_fn);
}

bool PrintHostPrusaLink::put_inner(
    std::string url,
    const std::string& name,
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn
) const
{
    //info_fn("set_complete_off", {});
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    bool res = true;
    // Percent escape all filenames in on path and add it to the url. This is different from POST.
    url += "/" + Network::IHttp::escape_path_by_element(m_upload_data.dest_path);

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Put,
        std::move(url),
        retry_fn
    );
#ifdef WIN32
    // "Host" header is necessary here. We have resolved IP address and substituted it into "url" variable.
    // And when creating Http object above, libcurl automatically includes "Host" header from address it got.
    // Thus "Host" is set to the resolved IP instead of host filled by user. We need to change it back.
    // Not changing the host would work on the most cases (where there is 1 service on 1 hostname) but would break when f.e. reverse proxy is used (issue #9734).
    // https://www.rfc-editor.org/rfc/rfc7230#section-5.4
    std::string host = Network::IHttp::extract_host_from_url(m_print_host_config.host);
    http->header("Host", host);
#endif // _WIN32
    set_auth(http.get());
    // There was an error at PrusaLink side that accepts any string at Print-After-Upload as true, thus False was also triggering print after upload.
    if (m_upload_data.post_action == PrintHostAfterUploadAction::StartPrint)
        http->header("Print-After-Upload", "?1");
    http->set_put_body(m_upload_data.source_path)
        .header("Content-Type", "text/x.gcode")
        .header("Overwrite", "?1")
        .on_complete([&](std::string body, unsigned status) {
            SPDLOG_INFO("{}: File uploaded: HTTP {}: {}", name, status, body);
            //info_fn("complete", body);
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error uploading file: , HTTP {}, body: `{}`", name, error, status, body);
            error_fn(format_error(body, error, status));
            res = false;
        })
        .on_progress([&](Network::IHttp::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                SPDLOG_INFO("PrusaLink: Upload canceled");
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

bool PrintHostPrusaLink::post_inner(
    std::string url,
    const std::string& name,
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn
) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    //info_fn("set_complete_off", {});
    bool res                      = true;
    const auto upload_filename    = m_upload_data.dest_path.filename();
    const auto upload_parent_path = m_upload_data.dest_path.parent_path();

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Post,
        std::move(url),
        retry_fn
    );
#ifdef WIN32
    // "Host" header is necessary here. We have resolved IP address and subsituted it into "url" variable.
    // And when creating Http object above, libcurl automatically includes "Host" header from address it got.
    // Thus "Host" is set to the resolved IP instead of host filled by user. We need to change it back.
    // Not changing the host would work on the most cases (where there is 1 service on 1 hostname) but would break when f.e. reverse proxy is used (issue #9734).
    // https://www.rfc-editor.org/rfc/rfc7230#section-5.4
    std::string host = Network::IHttp::extract_host_from_url(m_print_host_config.host);
    http->header("Host", host);
#endif // _WIN32
    set_auth(http.get());
    set_http_post_header_args(http.get(), m_upload_data.post_action);
    http->form_add("path", upload_parent_path.string()) // XXX: slashes on windows ???
        .form_add_file("file", m_upload_data.source_path, upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            // PrusaConnect message
            SPDLOG_INFO("{}: File uploaded: HTTP {}: {}", name, status, body);
            //if (status == 202)
            //    info_fn("complete_with_warning", body);
            //else
            //    info_fn("complete", body);
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
                SPDLOG_INFO("PrusaLink: Upload canceled");
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

void PrintHostPrusaLink::set_http_post_header_args(
    Network::IHttp* http,
    PrintHostAfterUploadAction action
) const
{
    http->form_add("print", action == PrintHostAfterUploadAction::StartPrint ? "true" : "false");
}

} // namespace Slic3r::Biz::PrintHost
