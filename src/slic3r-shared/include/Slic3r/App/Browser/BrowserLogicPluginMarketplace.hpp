#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Lua/PluginSystem.hpp"

namespace Slic3r::App::Browser {

class BrowserLogicPluginMarketplace final : public AbstractBrowserLogic
{
public:
    explicit BrowserLogicPluginMarketplace(Biz::ProjectInteractor& project_interactor, Lua::PluginSystem& plugin_system);

    std::vector<BrowserLogicCommand> on_script_message_webview_event(
        const std::string& message
    ) override;
    std::vector<BrowserLogicCommand> on_navigation_request_webview_event(
        const std::string& new_url,
        const std::string& current_url
    ) override;

    std::pair<int, int> size(int em_unit) const override;
    std::pair<int, int> min_size(int em_unit) const override;

    /**
     * @brief A (possibly refreshed) access token is available - push it to the web app.
     */
    std::vector<BrowserLogicCommand> on_user_account_id_success(bool is_refresh, const std::string& current_url) override;

    /**
     * @brief Account signed out - let the web app drop its marketplace session.
     */
    std::vector<BrowserLogicCommand> on_user_account_logged_out(const std::string& current_url) override;

    std::vector<BrowserLogicCommand> on_webview_created() override;

    bool requires_native_script_run() const override;

private:
    /**
     * @brief Answer REQUEST_CONFIG / REQUEST_ACCESS_TOKEN. Stays silent while a token
     * refresh is in flight; on_user_account_id_success() delivers the fresh token.
     * @param reason Action name, for logging only.
     */
    std::vector<BrowserLogicCommand> send_config(const std::string& reason);

    /**
     * @brief `_pluginMarketplace_v1.init(...)` call. Pass an empty token to hand over
     * only the plugin API version (anonymous browsing).
     */
    std::string init_script(const std::string& access_token) const;

    /**
     * `_pluginMarketplace_v1.setInstalledPlugins(...)` carrying the full current
     * inventory, so the catalogue can show Installed / Update instead of Install.
     */
    std::string installed_plugins_script(const std::vector<Lua::InstalledBundle>& bundles) const;

    Biz::ProjectInteractor& m_project_interactor;
    Lua::PluginSystem& m_plugin_system;
};

} // namespace Slic3r::App::Browser
