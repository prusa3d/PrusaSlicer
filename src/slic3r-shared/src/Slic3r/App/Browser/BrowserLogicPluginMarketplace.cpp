#include "Slic3r/App/Browser/BrowserLogicPluginMarketplace.hpp"

#include <nlohmann/json.hpp>

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Log.hpp"

namespace Slic3r::App::Browser {

namespace {
// Plugin API this Slicer build speaks (semver). The marketplace filters the
// catalogue by it - see docs/slicer-webview.md in the marketplace repo.
// Bump together with the Lua plugin API.
constexpr const char* PLUGIN_API_VERSION = "1.0.0";
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
    return "window._pluginMarketplace_v1 && window._pluginMarketplace_v1.init(" + options.dump() + ")";
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
        SPDLOG_INFO("Plugin Marketplace: install {}@{}", result->plugin_id, result->version);
        m_plugin_system.install_marketplace_bundle(result.value());
        return {};
    }

    if (action == "DOWNLOAD") {
        std::string url     = msg["url"].get<std::string>();
        std::string id      = msg["id"].get<std::string>();
        std::string version = msg["version"].get<std::string>();
        SPDLOG_DEBUG("Download message: URL: {}", url);
        m_plugin_system.download_file(id, version, url);

        return {};
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

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::
    on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url)
{
    return AbstractBrowserLogic::on_navigation_request_webview_event(new_url, current_url);
}

} // namespace Slic3r::App::Browser
