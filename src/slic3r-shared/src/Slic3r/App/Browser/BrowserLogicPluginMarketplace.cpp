#include "Slic3r/App/Browser/BrowserLogicPluginMarketplace.hpp"

#include <nlohmann/json.hpp>

#include "Slic3r/Biz/Network/ServiceConfig.hpp"

namespace Slic3r::App::Browser {

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

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::on_script_message_webview_event(
    const std::string& message
)
{
    std::string action;
    nlohmann::json msg = nlohmann::json::parse(message);
    if (auto it = msg.find("action"); it != msg.end()) {
        it->get_to(action);
    }

    // FE → Slicer (_prusaSlicer.postMessage)
    //
    // Host bridge detected
    // { action: 'WEBAPP_READY', componentName: 'plugin-marketplace' }
    //
    // Host bridge detected
    // { action: 'REQUEST_CONFIG' }
    //
    // /auth/slicer-session fails
    // { action: 'REQUEST_ACCESS_TOKEN' }
    //
    // User taps Install
    // { action: 'INSTALL_PLUGIN', pluginId, version, downloadUrl, verifyUrl, apiOrigin }

    if (action == "WEBAPP_READY") {
        SPDLOG_INFO("Plugin marketplace loaded");
    } else if (action == "REQUEST_CONFIG") {
        SPDLOG_INFO("Plugin market place request config");
        auto access_token = m_project_interactor.user_account_interactor().access_token();
        return {BrowserLogicCommand{
            BrowserLogicCommandType::RunScript,
            std::string("_pluginMarketplace_v1.init({ accessToken: '") + access_token + "', pluginApiVersion: '1.0.0' })"
        }};
    } else if (action == "REQUEST_ACCESS_TOKEN") {
        SPDLOG_INFO("Plugin market place request token");
        auto access_token = m_project_interactor.user_account_interactor().access_token();
        return {BrowserLogicCommand{
            BrowserLogicCommandType::RunScript,
            std::string("window._pluginMarketplace_v1.init({ accessToken: '") + access_token + "', pluginApiVersion: '1.0.0' }); console.log('token passed');"
        }};
    } else if (action == "INSTALL_PLUGIN") {
        auto result = Lua::BundleRequestConfig::load_from_json(msg);
        if (!result.has_value()) {
            SPDLOG_ERROR("Cannot parse message passed: {}", message);
        }
        SPDLOG_INFO("Plugin Marketplace Install plugin: {}", result->plugin_id);
        m_plugin_system.install_marketplace_bundle(result.value());
    } else {
        SPDLOG_INFO("Plugin Marketplace: Unknown action {}", action);
    }

    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPluginMarketplace::
    on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url)
{
    return AbstractBrowserLogic::on_navigation_request_webview_event(new_url, current_url);
}

} // namespace Slic3r::App::Browser
