#include "Slic3r/Biz/Network/ServiceConfig.hpp"

#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/Network/Url.hpp"
#include "Slic3r/Log.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <locale>
#include <mutex>
#include <vector>

namespace Slic3r::Biz::Network {

namespace {
constexpr std::string_view CONNECT_LOCAL_SELECT_PRINTER_PATH =
    "/m/com.github.aux.prusa3d.prusaslicer3-connector/select-printer?embed=1";
constexpr std::string_view CONNECT_LOCAL_CONNECT_PAGE_PATH = "/";
constexpr std::string_view CONNECT_LOCAL_ACCOUNT_API_PATH  = "/account";
constexpr std::string_view CONNECT_LOCAL_CONNECT_API_PATH  = "/connect";
constexpr std::string_view CONNECT_LOCAL_PRINTER_LIST_PATH = "/slicer/v2/printers";
constexpr std::string_view CONNECT_LOCAL_PAIRING_API_PATH  = "/api/v1/auth/app-pairings";
constexpr std::string_view CONNECT_LOCAL_CREDENTIALS_PATH  = "/api/v1/auth/credentials/";
constexpr std::string_view CONNECT_LOCAL_PAIRING_PAGE_PATH = "/pair?code=";
constexpr std::string_view CONNECT_LOCAL_PRINTER_PAGE_PATH = "/printers/";

void update_from_env(std::string& dest, const char* env_name, bool remove_trailing_slash)
{
    const char* env_val = std::getenv(env_name);
    if (env_val == nullptr || std::strlen(env_val) == 0)
        return;

    dest = env_val;
    if (remove_trailing_slash) {
        auto idx = dest.find_last_not_of('/');
        if (idx != std::string::npos && idx + 1 < dest.length())
            dest.erase(idx + 1, std::string::npos);
    }
}


bool is_whitelisted_url(const std::string& url, const std::vector<std::string>& allowed_domains) 
{
    const std::string host = IHttp::get_apex_domain(url);
    return std::ranges::find(allowed_domains, host) != allowed_domains.end();
}

void update_url_from_env(std::string& dest, const char* env_name, const std::vector<std::string>& allowed_domains)
{
    std::string candidate = dest;
    update_from_env(candidate, env_name, true);
    
    if (is_whitelisted_url(candidate, allowed_domains)) {
        dest = std::move(candidate);
    } else {
        SPDLOG_WARN(
            "Ignoring {}={}: apex domain '{}' is not whitelisted, keeping {}.",
            env_name,
            candidate,
            IHttp::get_apex_domain(candidate),
            dest
        );
    }
}

/** @brief Whether the text consists of ASCII letters, digits and the given characters only. */
bool is_ascii_alnum_or(std::string_view text, const char* others)
{
    using namespace boost::algorithm;
    return all(text, is_alnum(std::locale::classic()) || is_any_of(others));
}
}

ServiceConfig::ServiceConfig() :
    m_connect_url("https://connect.prusa3d.com"),
    m_connect_api_url("https://connect-api.prusa3d.com"),
    m_connect_mqtt_url("wss://mqtt.prusa3d.com:8084/mqtt"),
    m_account_url("https://account.prusa3d.com"),
    m_account_client_id("oamhmhZez7opFosnwzElIgE2oGgI2iJORSkw587O"),
    m_media_url("https://media.printables.com"),
    m_preset_repo_url("https://preset-repo-api.prusa3d.com"),
    m_printables_url("https://www.printables.com"),
    m_plugin_marketplace_url("https://plugin-marketplace.prusa3d.com/?webview=1")
{
#ifdef SLIC3R_REPO_URL
    m_preset_repo_url = SLIC3R_REPO_URL;
#endif

    const std::vector<std::string> all_domains = {
        "prusa.com",
        "prusa3d.com",
        "prusa.cz",
        "prusa3d.cz",
        "prusa3d.dev",
        "printables.com",
        "testprusaverse.com",
#if !defined(NDEBUG) || defined(SLIC3R_ALLOW_LOCALHOST_SERVICES)
        // Local development. get_apex_domain() keeps only the last two labels,
        // so "localhost" passes but "127.0.0.1" does not - use localhost.
        "localhost",
#endif
    };
    update_url_from_env(m_connect_url, "PRUSA_CONNECT_URL", all_domains);
    update_url_from_env(m_connect_api_url, "PRUSA_CONNECT_API_URL", all_domains);
    update_url_from_env(m_connect_mqtt_url, "PRUSA_CONNECT_MQTT_URL", all_domains);
    update_url_from_env(m_account_url, "PRUSA_ACCOUNT_URL", all_domains);
    update_url_from_env(m_media_url, "PRUSA_MEDIA_URL", all_domains);
    update_url_from_env(m_preset_repo_url, "PRUSA_PRESET_REPO_URL", all_domains);
    update_url_from_env(m_printables_url, "PRUSA_PRINTABLES_URL", all_domains);
    update_url_from_env(m_plugin_marketplace_url, "PRUSA_PLUGIN_MARKETPLACE_URL", all_domains);

    update_from_env(m_account_client_id, "PRUSA_ACCOUNT_CLIENT_ID", false);

    m_mode_urls = cloud_urls();
}

ServiceConfig& ServiceConfig::instance()
{
    static ServiceConfig inst;
    return inst;
}


std::optional<std::string> ServiceConfig::normalize_connect_local_server_url(std::string_view url)
{
    using namespace boost::algorithm;
    const std::locale& ascii  = std::locale::classic();
    const std::string trimmed = trim_copy(std::string(url), ascii);
    if (trimmed.empty()
        || std::ranges::any_of(trimmed, is_space(ascii) || is_cntrl(ascii) || is_any_of("?#@\\")))
    {
        return std::nullopt;
    }
    const bool has_scheme               = trimmed.find("://") != std::string::npos;
    const std::optional<UrlParts> parts = parse_url(has_scheme ? trimmed : "http://" + trimmed);
    if (!parts || (parts->scheme != "http" && parts->scheme != "https")) {
        return std::nullopt;
    }
    if (!parts->is_ipv6() && !all(parts->host, is_alnum(ascii) || is_any_of("-._"))) {
        return std::nullopt;
    }
    std::string_view path = parts->path;
    while (path.ends_with('/')) {
        path.remove_suffix(1);
    }
    UrlParts server      = *parts;
    server.explicit_port = parts->explicit_port && parts->port != (parts->scheme == "https" ? 443 : 80);
    return server.origin() + std::string(path);
}

void ServiceConfig::set_mode(ServiceMode mode, const std::string& connect_local_server_url)
{
    ModeUrls urls = cloud_urls();
    if (mode == ServiceMode::ConnectLocal) {
        const std::optional<std::string> server = normalize_connect_local_server_url(connect_local_server_url);
        if (!server) {
            SPDLOG_WARN("Invalid Prusa Connect on-premise server URL '{}'.", connect_local_server_url);
        }
        urls = connect_local_urls(server.value_or(std::string()));
    }
    SPDLOG_INFO(
        "Service mode: {}{}",
        mode == ServiceMode::Cloud ? "cloud" : "on-premise",
        mode == ServiceMode::Cloud ? std::string() : ", server: " + urls.connect_local_server
    );

    std::unique_lock lock(m_mutex);
    m_mode_urls = std::move(urls);
}

ServiceConfig::ModeUrls ServiceConfig::cloud_urls() const
{
    return {
        .mode                   = ServiceMode::Cloud,
        .connect_local_server         = {},
        .connect                = m_connect_url,
        .connect_api            = m_connect_api_url,
        .connect_printer_list   = m_connect_api_url + "/printer/slicer/v1/printers",
        .connect_mqtt           = m_connect_mqtt_url,
        .connect_graphql        = m_connect_api_url + "/graphql",
        .connect_printables_print = m_connect_url + "/slicer-print",
        .connect_page           = m_connect_url,
        .connect_select_printer = m_connect_url + "/slicer-select-printer",
        .account                = m_account_url,
        .account_token          = m_account_url + "/o/token/",
    };
}

ServiceConfig::ModeUrls ServiceConfig::connect_local_urls(const std::string& server)
{
    if (server.empty()) {
        return {.mode = ServiceMode::ConnectLocal};
    }
    const std::string module  = server + std::string(CONNECT_LOCAL_MODULE_PATH);
    const std::string connect = module + std::string(CONNECT_LOCAL_CONNECT_API_PATH);
    return {
        .mode                   = ServiceMode::ConnectLocal,
        .connect_local_server         = server,
        .connect                = connect,
        .connect_api            = {},
        .connect_printer_list   = connect + std::string(CONNECT_LOCAL_PRINTER_LIST_PATH),
        .connect_mqtt           = {},
        .connect_graphql        = {},
        .connect_printables_print = {},
        .connect_page           = server + std::string(CONNECT_LOCAL_CONNECT_PAGE_PATH),
        .connect_select_printer = server + std::string(CONNECT_LOCAL_SELECT_PRINTER_PATH),
        .account                = module + std::string(CONNECT_LOCAL_ACCOUNT_API_PATH),
        .account_token = {},
    };
}

std::string ServiceConfig::mode_url(std::string ModeUrls::* url) const
{
    std::shared_lock lock(m_mutex);
    return m_mode_urls.*url;
}

ServiceMode ServiceConfig::mode() const
{
    std::shared_lock lock(m_mutex);
    return m_mode_urls.mode;
}

std::string ServiceConfig::connect_local_server_url() const
{
    return mode_url(&ModeUrls::connect_local_server);
}


std::string ServiceConfig::connect_url() const
{
    return mode_url(&ModeUrls::connect);
}

std::string ServiceConfig::connect_api_url() const
{
    return mode_url(&ModeUrls::connect_api);
}

std::string ServiceConfig::connect_printer_list_url() const
{
    return mode_url(&ModeUrls::connect_printer_list);
}

std::string ServiceConfig::connect_mqtt_url() const
{
    return mode_url(&ModeUrls::connect_mqtt);
}

std::string ServiceConfig::connect_graphql_url() const
{
    return mode_url(&ModeUrls::connect_graphql);
}

std::string ServiceConfig::connect_printables_print_url() const
{
    return mode_url(&ModeUrls::connect_printables_print);
}

std::string ServiceConfig::connect_organization_files_url(const std::string& organization_id) const
{
    const std::string api = connect_api_url();
    if (api.empty()) {
        return {};
    }
    return api + "/file/slicer/v1/organizations/" + organization_id + "/files";
}

std::string ServiceConfig::connect_organization_file_url(
    const std::string& organization_id,
    const std::string& file_id
) const
{
    const std::string files = connect_organization_files_url(organization_id);
    if (files.empty()) {
        return {};
    }
    return files + "/" + file_id;
}

std::string ServiceConfig::connect_printer_queue_url(const std::string& printer_uuid) const
{
    const std::string api = connect_api_url();
    if (api.empty()) {
        return {};
    }
    return api + "/printer/slicer/v1/printers/" + printer_uuid + "/queue";
}

std::string ServiceConfig::connect_page_url() const
{
    return mode_url(&ModeUrls::connect_page);
}

std::string ServiceConfig::connect_select_printer_url() const
{
    return mode_url(&ModeUrls::connect_select_printer);
}

std::string ServiceConfig::connect_printer_page_url(const std::string& printer_uuid) const
{
    if (printer_uuid.empty() || !is_ascii_alnum_or(printer_uuid, "-_")) {
        return {};
    }
    std::shared_lock lock(m_mutex);
    if (m_mode_urls.mode == ServiceMode::Cloud) {
        return m_mode_urls.connect + "/printer/" + printer_uuid + "/dashboard";
    }
    if (m_mode_urls.connect_local_server.empty()) {
        return {};
    }
    return m_mode_urls.connect_local_server + std::string(CONNECT_LOCAL_PRINTER_PAGE_PATH) + printer_uuid;
}


std::string ServiceConfig::account_url() const
{
    return mode_url(&ModeUrls::account);
}

std::string ServiceConfig::account_token_url() const
{
    return mode_url(&ModeUrls::account_token);
}


std::string ServiceConfig::connect_local_pairing_url() const
{
    const std::string server = connect_local_server_url();
    return server.empty() ? std::string() : server + std::string(CONNECT_LOCAL_PAIRING_API_PATH);
}

std::string ServiceConfig::connect_local_pairing_token_url() const
{
    const std::string url = connect_local_pairing_url();
    return url.empty() ? std::string() : url + "/token";
}

std::string ServiceConfig::connect_local_pairing_page_url(const std::string& user_code) const
{
    const std::string server = connect_local_server_url();
    if (server.empty() || user_code.empty() || !is_ascii_alnum_or(user_code, "-")) {
        return {};
    }
    return server + std::string(CONNECT_LOCAL_PAIRING_PAGE_PATH) + user_code;
}

std::string ServiceConfig::connect_local_page_session_url() const
{
    const std::string url = connect_local_pairing_url();
    return url.empty() ? std::string() : url + "/session";
}

std::string ServiceConfig::connect_local_credential_url(const std::string& credential_id) const
{
    const std::string server = connect_local_server_url();
    if (server.empty() || credential_id.empty() || !is_ascii_alnum_or(credential_id, "-_")) {
        return {};
    }
    return server + std::string(CONNECT_LOCAL_CREDENTIALS_PATH) + credential_id;
}

} // namespace Slic3r::Biz::Network
