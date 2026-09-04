#include "Slic3r/Biz/PrintHost/PrintHostPrusaLinkStorage.hpp"

#include "Slic3r/Log.hpp"

#include "fmt/format.h"
#include <nlohmann/json.hpp>

#include "Slic3r/LegacyFormat.hpp"

namespace fs = boost::filesystem;

namespace Slic3r::Biz::PrintHost {

PrintHostPrusaLinkStorage::PrintHostPrusaLinkStorage(PhysicalPrinter::PhysicalPrinterConfig config, PrintHostJobData data) :
    IPrintHost(std::move(config), std::move(data))
{}

bool PrintHostPrusaLinkStorage::perform(
    ProgressFn progress_fn,
    RetryFn retry_fn,
    ErrorFn error_fn,
    InfoFn info_fn
) const
{
    const PhysicalPrinter::PrinterUpload* auth = std::get_if<PhysicalPrinter::PrinterUpload>(&m_print_host_config.payload);
    ASSERT(auth);

    const char* name = get_name();

    bool res = true;
    auto url = make_url("api/v1/storage");
    std::string error_msg;
    std::string storage_result;

    SPDLOG_INFO("{}: Get storage at: {}", name, url);

    // TODO: get language from somewhere
    std::string lang = "en";

    std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
        Network::IHttp::RequestMethod::Get,
        std::move(url),
        retry_fn
    );
    std::string auth_err_msg;
    if (!set_auth(http.get(), auth_err_msg)) {
        error_fn(auth_err_msg);
        return false;
    }
    http->header("Accept-Language", lang);
    http->on_error([&](std::string body, std::string error, unsigned status) {
            SPDLOG_ERROR("{}: Error getting storage: {}, HTTP {}, body: `{}`", name, error, status, body);
            error_msg = "\n\n" + error;
            res       = false;
            // If status is 0, the communication with the printer has failed completely (most likely a timeout), if the status is <= 400, it is an error returned by the printer.
            // If 0, we can show error to the user now, as we know the communication has failed.
            // if not 0, we must not show error, as not all printers support api/v1/storage endpoint.
            // So we must be extra careful here, or we might be showing errors on perfectly fine communication.
            if (status != 0) {
                res = true;
                return;
            }
            error_fn(error);
        })
        .on_complete([&](std::string body, unsigned) {
            SPDLOG_INFO("{}: Got storage: {}", name, body);
            try {
                nlohmann::json json = nlohmann::json::parse(body);

                if (!json.contains("storage_list")) {
                    res = false;
                    return;
                }
                storage_result = json["storage_list"].dump();
            } catch (const nlohmann::json::exception&) {
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(auth->ssl_revoke_best_effort)
#endif // WIN32
        .perform_sync();

    if (res) {
        info_fn(PrintHostJobInfoTag::Storage, storage_result);
    }

    return res;
}

std::string PrintHostPrusaLinkStorage::make_url(const std::string& path) const
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

bool PrintHostPrusaLinkStorage::set_auth(Network::IHttp* http, std::string& err_msg) const
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
        err_msg = _u8L("PrusaLink does not support other authentication method than api-key or http digest.");
        return false;
    }

    if (!auth->ca_file.empty()) {
        http->ca_file(auth->ca_file);
    }
    return true;
}
} // namespace Slic3r::Biz::PrintHost
