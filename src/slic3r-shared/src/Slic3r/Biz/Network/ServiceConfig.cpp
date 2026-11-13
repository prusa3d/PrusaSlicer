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
// Offline (Connect Local) URL layout. S is the user entered server URL, M is S + OFFLINE_MODULE_PATH.
// Web pages are served from S, the HTTP API of the PrusaSlicer module from M, app pairing
// (the offline sign-in) from the core API of S.
constexpr std::string_view OFFLINE_SELECT_PRINTER_PATH =
    "/m/com.github.aux.prusa-research.prusaslicer3-compat/select-printer?embed=1"; // S +
constexpr std::string_view OFFLINE_CONNECT_PAGE_PATH = "/"; // S +
constexpr std::string_view OFFLINE_ACCOUNT_API_PATH  = "/account"; // M +
constexpr std::string_view OFFLINE_CONNECT_API_PATH  = "/connect"; // M +
constexpr std::string_view OFFLINE_PAIRING_API_PATH  = "/api/v1/auth/app-pairings"; // S +
constexpr std::string_view OFFLINE_CREDENTIALS_PATH  = "/api/v1/auth/credentials/"; // S +
constexpr std::string_view OFFLINE_PAIRING_PAGE_PATH = "/pair?code="; // S +

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

    m_mode_urls = online_urls();
}

ServiceConfig& ServiceConfig::instance()
{
    static ServiceConfig inst;
    return inst;
}

// ---- Service mode ----

std::optional<std::string> ServiceConfig::normalize_offline_server_url(std::string_view url)
{
    using namespace boost::algorithm;
    const std::locale& ascii  = std::locale::classic();
    const std::string trimmed = trim_copy(std::string(url), ascii);
    // Neither query, fragment nor user info, nothing curl would escape or reject.
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
    return parts->origin() + std::string(path);
}

void ServiceConfig::set_mode(ServiceMode mode, const std::string& offline_server_url)
{
    ModeUrls urls = online_urls();
    if (mode == ServiceMode::Offline) {
        const std::optional<std::string> server = normalize_offline_server_url(offline_server_url);
        if (!server) {
            SPDLOG_WARN("Offline mode: invalid Connect Local server URL '{}'.", offline_server_url);
        }
        urls = offline_urls(server.value_or(std::string()));
    }
    SPDLOG_INFO(
        "Service mode: {}{}",
        mode == ServiceMode::Online ? "online" : "offline",
        mode == ServiceMode::Online ? std::string() : ", server: " + urls.offline_server
    );

    std::unique_lock lock(m_mutex);
    m_mode_urls = std::move(urls);
}

ServiceConfig::ModeUrls ServiceConfig::online_urls() const
{
    return {
        .mode                   = ServiceMode::Online,
        .offline_server         = {},
        .connect                = m_connect_url,
        .connect_page           = m_connect_url,
        .connect_select_printer = m_connect_url + "/slicer-select-printer",
        .account                = m_account_url,
        .account_token          = m_account_url + "/o/token/",
    };
}

ServiceConfig::ModeUrls ServiceConfig::offline_urls(const std::string& server)
{
    if (server.empty()) {
        return {.mode = ServiceMode::Offline};
    }
    const std::string module = server + std::string(OFFLINE_MODULE_PATH);
    return {
        .mode                   = ServiceMode::Offline,
        .offline_server         = server,
        .connect                = module + std::string(OFFLINE_CONNECT_API_PATH),
        .connect_page           = server + std::string(OFFLINE_CONNECT_PAGE_PATH),
        .connect_select_printer = server + std::string(OFFLINE_SELECT_PRINTER_PATH),
        .account                = module + std::string(OFFLINE_ACCOUNT_API_PATH),
        // Connect Local signs in by app pairing, there is no OAuth token endpoint.
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

std::string ServiceConfig::offline_server_url() const
{
    return mode_url(&ModeUrls::offline_server);
}

// ---- Prusa Connect ----

std::string ServiceConfig::connect_url() const
{
    return mode_url(&ModeUrls::connect);
}

std::string ServiceConfig::connect_page_url() const
{
    return mode_url(&ModeUrls::connect_page);
}

std::string ServiceConfig::connect_select_printer_url() const
{
    return mode_url(&ModeUrls::connect_select_printer);
}

// ---- Prusa Account ----

std::string ServiceConfig::account_url() const
{
    return mode_url(&ModeUrls::account);
}

std::string ServiceConfig::account_token_url() const
{
    return mode_url(&ModeUrls::account_token);
}

// ---- Connect Local app pairing ----

std::string ServiceConfig::offline_pairing_url() const
{
    const std::string server = offline_server_url();
    return server.empty() ? std::string() : server + std::string(OFFLINE_PAIRING_API_PATH);
}

std::string ServiceConfig::offline_pairing_token_url() const
{
    const std::string url = offline_pairing_url();
    return url.empty() ? std::string() : url + "/token";
}

std::string ServiceConfig::offline_pairing_page_url(const std::string& user_code) const
{
    // The code is the only part of the URL taken from the server, keep it to the characters a user
    // code consists of, so it cannot add anything to the URL.
    const std::string server = offline_server_url();
    if (server.empty() || user_code.empty() || !is_ascii_alnum_or(user_code, "-")) {
        return {};
    }
    return server + std::string(OFFLINE_PAIRING_PAGE_PATH) + user_code;
}

std::string ServiceConfig::offline_page_session_url() const
{
    const std::string url = offline_pairing_url();
    return url.empty() ? std::string() : url + "/session";
}

std::string ServiceConfig::offline_credential_url(const std::string& credential_id) const
{
    const std::string server = offline_server_url();
    if (server.empty() || credential_id.empty() || !is_ascii_alnum_or(credential_id, "-_")) {
        return {};
    }
    return server + std::string(OFFLINE_CREDENTIALS_PATH) + credential_id;
}

} // namespace Slic3r::Biz::Network
