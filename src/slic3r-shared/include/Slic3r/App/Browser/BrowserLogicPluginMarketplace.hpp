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

private:
    Biz::ProjectInteractor& m_project_interactor;
    Lua::PluginSystem& m_plugin_system;
};

} // namespace Slic3r::App::Browser
