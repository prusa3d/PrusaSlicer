#include "Slic3r/App/Browser/BrowserLogicPluginMarketplace.hpp"

#include <algorithm>
#include <cctype>

#include <boost/filesystem/path.hpp>
#include <nlohmann/json.hpp>

#include "Slic3r/App/Lua/Plugin.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::App::Browser {

namespace {
// Plugin API this Slicer build speaks (semver). The marketplace filters the
// catalogue by it - see docs/slicer-webview.md in the marketplace repo.
// Bump together with the Lua plugin API.
constexpr const char* PLUGIN_API_VERSION = "1.0.0";

/** Lower-cased scheme://host[:port] of `url`; empty when `url` has no scheme. */
std::string origin_of(const std::string& url)
{
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return {};
    }
    const auto path_start = url.find_first_of("/?#", scheme_end + 3);
    std::string origin = url.substr(0, path_start);
    std::ranges::transform(origin, origin.begin(), [](unsigned char c) { return std::tolower(c); });
    return origin;
}

/** True if `url` has the same (non-empty) origin as `marketplace_url`. */
bool is_marketplace_url(const std::string& url, const std::string& marketplace_url)
{
    const std::string origin = origin_of(marketplace_url);
    return !origin.empty() && origin_of(url) == origin;
}

bool is_http_url(const std::string& url)
{
    const std::string origin = origin_of(url);
    return origin.starts_with("http://") || origin.starts_with("https://");
}

/**
 * The exact loading/error page URL the webview host builds (WebViewPanel / WebViewDialog):
 * "file://" + generic(resources_dir()) + "/web/" + page + {"" | "_dark"} + ".html".
 * Also accepts the "file:///C:/..." spelling WebView2 reports back for a "file://C:/..." load.
 */
bool is_local_web_page(const std::string& url, const std::string& page)
{
    const std::string path = boost::filesystem::path(resources_dir()).generic_string() + "/web/" + page;
    for (const char* suffix : {".html", "_dark.html"}) {
        const std::string file = path + suffix;
        if (url == "file://" + file || url == "file:///" + file) {
            return true;
        }
    }
    return false;
}
} // namespace

BrowserLogicPluginMarketplace::BrowserLogicPluginMarketplace(Biz::ProjectInteractor& project_interactor, Lua::PluginSystem& plugin_system) :
    AbstractBrowserLogic(
        Biz::Network::ServiceConfig::instance().plugin_marketplace_url(),
        {"_prusaSlicer"},
        "other_loading",
        "other_error",
        "Plugin Marketplace"
    ),
    m_project_interactor(project_interactor),
    m_plugin_system(plugin_system)
{}

std::string BrowserLogicPluginMarketplace::init_script(const std::string& access_token) const
{
    // Build the options through nlohmann so the token is JSON-escaped instead of
    // being pasted straight into a JS string literal.
    nlohmann::json options;
    options["pluginApiVersion"] = PLUGIN_API_VERSION;
    if (!access_token.empty()) {
        options["accessToken"] = access_token;
    }
    // The web app registers _pluginMarketplace_v1 on load; guard so an unsolicited
    // push (token refresh) before that cannot throw inside the webview.
    return "if (window._pluginMarketplace_v1) { window._pluginMarketplace_v1.init(" + options.dump() + "); console.log('PM init sent'); }";
}

std::string BrowserLogicPluginMarketplace::installed_plugins_script(
    const std::vector<Lua::InstalledBundle>& bundles
) const
{
    nlohmann::json plugins = nlohmann::json::array();
    for (const auto& bundle : bundles) {
        plugins.push_back({{"id", bundle.id}, {"version", bundle.version}});
    }
    nlohmann::json payload;
    payload["plugins"] = std::move(plugins);
    return "window._pluginMarketplace_v1 && window._pluginMarketplace_v1.setInstalledPlugins("
        + payload.dump() + ")";
}

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::send_config(const std::string& reason)
{
    auto& account = m_project_interactor.user_account_interactor();

    if (!account.is_logged_in()) {
        // Hand over the plugin API version anyway so the catalogue can filter;
        // without a token the web app stays on its anonymous path instead of
        // retrying the session exchange.
        SPDLOG_DEBUG("Plugin Marketplace: {} with no signed-in account, sending config without a token", reason);
        return {{BrowserLogicCommandType::RunScript, init_script({})}};
    }

    if (account.validate_and_refresh()) {
        // Token is expired and a refresh was just queued. Answering with the stale
        // token is what made the web app and the host ping-pong REQUEST_ACCESS_TOKEN
        // forever; on_user_account_id_success() pushes the fresh one once it lands.
        SPDLOG_INFO("Plugin Marketplace: {} while the access token is refreshing, deferring", reason);
        return {};
    }

    const std::string access_token = account.access_token();
    if (access_token.empty()) {
        SPDLOG_WARN("Plugin Marketplace: {} but the access token is empty, sending config without a token", reason);
        return {{BrowserLogicCommandType::RunScript, init_script({})}};
    }

    return {{BrowserLogicCommandType::RunScript, init_script(access_token)}};
}

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::on_script_message_webview_event(
    const std::string& message
)
{
    // FE -> Slicer (_prusaSlicer.postMessage)
    //
    // { action: 'WEBAPP_READY', componentName: 'plugin-marketplace' }
    // { action: 'REQUEST_CONFIG' }
    // { action: 'REQUEST_ACCESS_TOKEN' }                        // session exchange failed
    // { action: 'REQUEST_INSTALLED_PLUGINS' }                    // which plugins are installed
    // { action: 'INSTALL_PLUGIN', pluginId, version, downloadUrl, verifyUrl, apiOrigin }
    // { action: 'DOWNLOAD', id, version, url }                  // save a bundle to the download folder

    nlohmann::json msg;
    std::string action;
    try {
        msg = nlohmann::json::parse(message);
        if (auto it = msg.find("action"); it != msg.end() && it->is_string()) {
            it->get_to(action);
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Plugin Marketplace: cannot parse webview message ({}): {}", e.what(), message);
        return {};
    }

    if (action.empty()) {
        SPDLOG_ERROR("Plugin Marketplace: webview message without an action: {}", message);
        return {};
    }

    if (action == "WEBAPP_READY") {
        SPDLOG_DEBUG("Plugin Marketplace: web app ready");
        return {};
    }

    if (action == "REQUEST_CONFIG" || action == "REQUEST_ACCESS_TOKEN") {
        SPDLOG_DEBUG("Plugin Marketplace: {}", action);
        return send_config(action);
    }

    if (action == "REQUEST_INSTALLED_PLUGINS") {
        const auto bundles = m_plugin_system.installed_bundles();
        SPDLOG_DEBUG("Plugin Marketplace: reporting {} installed bundle(s)", bundles.size());
        return {{BrowserLogicCommandType::RunScript, installed_plugins_script(bundles)}};
    }

    if (action == "INSTALL_PLUGIN") {
        auto result = Lua::BundleRequestConfig::load_from_json(msg);
        if (!result.has_value()) {
            SPDLOG_ERROR("Plugin Marketplace: cannot parse INSTALL_PLUGIN ({}): {}", result.error(), message);
            return {};
        }
        // verifyUrl hands over the public key the bundle is checked against, so both it and
        // downloadUrl must point at the configured marketplace, not wherever the page says.
        if (!is_marketplace_url(result->verify_url, m_url) || !is_marketplace_url(result->download_url, m_url)) {
            SPDLOG_ERROR("Plugin Marketplace: INSTALL_PLUGIN URLs are not on {}: {}", origin_of(m_url), message);
            return {};
        }
        SPDLOG_INFO("Plugin Marketplace: install {}@{}", result->plugin_id, result->version);
        m_plugin_system.install_marketplace_bundle(result.value());
        return {};
    }

    if (action == "UNINSTALL_PLUGIN") {
        std::string bundle_id, version;
        try {
            msg.at("pluginId").get_to(bundle_id);
            msg.at("version").get_to(version);
        } catch (std::exception& e) {
            SPDLOG_ERROR("Plugin Marketplace: cannot parse UNINSTALL_PLUGIN: {}", e.what());
            return {};
        }
        SPDLOG_INFO("Plugin Marketplace: uninstall {}@{}", bundle_id, version);
        m_plugin_system.uninstall_bundle(bundle_id, version);
        return {};
    }

    if (action == "DOWNLOAD") {
        std::string url, id, version;
        try {
            msg.at("url").get_to(url);
            msg.at("id").get_to(id);
            msg.at("version").get_to(version);
        } catch (std::exception& e) {
            SPDLOG_ERROR("Plugin Marketplace: cannot parse DOWNLOAD: {}", e.what());
            return {};
        }
        // id and version name the saved file; the bundle must come from the marketplace.
        if (!Lua::is_safe_path_component(id) || !Lua::is_safe_path_component(version)
            || !is_marketplace_url(url, m_url)) {
            SPDLOG_ERROR("Plugin Marketplace: rejected DOWNLOAD: {}", message);
            return {};
        }
        SPDLOG_DEBUG("Download message: URL: {}", url);
        m_plugin_system.download_file(id, version, url);

        return {};
    }

    if (action == "OPEN_URL") {
        std::string url;
        try {
            msg.at("url").get_to(url);
        } catch (std::exception& e) {
            SPDLOG_ERROR("Plugin Marketplace: cannot parse OPEN_URL: {}", e.what());
            return {};
        }
        // Only web links go to the system browser; file:, ms-*: etc. would reach other protocol handlers.
        if (!is_http_url(url)) {
            SPDLOG_ERROR("Plugin Marketplace: rejected OPEN_URL: {}", url);
            return {};
        }
        return {{BrowserLogicCommandType::OpenExternalBrowser, url}};
    }

    SPDLOG_INFO("Plugin Marketplace: unknown action {}", action);
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::on_user_account_id_success(
    bool is_refresh,
    const std::string& current_url
)
{
    const std::string access_token = m_project_interactor.user_account_interactor().access_token();
    if (access_token.empty()) {
        return {};
    }
    SPDLOG_DEBUG("Plugin Marketplace: pushing {} access token", is_refresh ? "refreshed" : "new");
    return {{BrowserLogicCommandType::RunScript, init_script(access_token)}};
}

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::on_user_account_logged_out(
    const std::string& current_url
)
{
    return {
        {BrowserLogicCommandType::RunScript,
         "window._pluginMarketplace_v1 && window._pluginMarketplace_v1.logout()"}
    };
}

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::on_webview_created()
{
    return {{BrowserLogicCommandType::DeleteCookies, m_url}};
}

bool BrowserLogicPluginMarketplace::requires_native_script_run() const
{
    return true;
}

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::
    on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url)
{
    // Any page in this webview can post _prusaSlicer messages, so only the marketplace
    // itself and the host's local loading/error pages may be shown here.
    if (is_marketplace_url(new_url, m_url) || new_url == "about:blank"
        || is_local_web_page(new_url, m_loading_html) || is_local_web_page(new_url, m_error_html)) {
        return {};
    }
    SPDLOG_INFO("Plugin Marketplace: vetoing navigation to {}", new_url);
    return {{BrowserLogicCommandType::Veto, {}}};
}

std::pair<int, int> BrowserLogicPluginMarketplace::size(int em_unit) const
{
    return {130*em_unit, 100*em_unit};
}

std::pair<int, int> BrowserLogicPluginMarketplace::min_size(int em_unit) const
{
    return {40*em_unit, 40*em_unit};
}

} // namespace Slic3r::App::Browser
