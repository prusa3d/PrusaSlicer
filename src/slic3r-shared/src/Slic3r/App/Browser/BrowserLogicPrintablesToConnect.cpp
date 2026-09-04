#include "Slic3r/App/Browser/BrowserLogicPrintablesToConnect.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Biz/ProjectInteractor.hpp"

#include "Slic3r/Assert.hpp"

namespace Slic3r::App::Browser {


BrowserLogicPrintablesToConnect::BrowserLogicPrintablesToConnect(const std::string& url, Biz::ProjectInteractor& project_interactor)
    : AbstractBrowserLogic(url, {"_prusaSlicer"}, "connect_loading", "connect_error")
    , AbstractConnectRequestHandler(project_interactor)
{
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_script_message_webview_event(const std::string& message) 
{
     return handle_message(message);
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_show_webview_event(bool show)
{
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_connect_action_select_printer(const std::string& message_data)
{
    DEBUG_ASSERT(false, "SELECT_PRINTER request is not defined for Printables to Connect.");
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_connect_action_print(const std::string& message_data)
{
    DEBUG_ASSERT(false, "PRINT request is not defined for Printables to Connect.");
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_connect_action_webapp_ready(const std::string& message_data)
{
    DEBUG_ASSERT(false, "WEBAPP READY request is not defined for Printables to Connect.");
    return {};
    
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_webview_reload_event(const std::string& message_data)
{
    return {{BrowserLogicCommandType::LoadURL, m_url}};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_connect_action_close_dialog(const std::string& message_data)
{
    return {{BrowserLogicCommandType::EndModalOK, {}}};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintablesToConnect::on_connect_action_log_in_in_browser(const std::string& message_data)
{
    return {};
}
} // namespace Slic3r::App::Browser 