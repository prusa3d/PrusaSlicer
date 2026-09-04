#pragma once

#include "Slic3r/App/Browser/BrowserLogicCommand.hpp"

#include <map>
#include <string>
#include <functional>

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::Browser {
class AbstractConnectRequestHandler
{
public:
    AbstractConnectRequestHandler(Biz::ProjectInteractor& project_interactor);
    virtual ~AbstractConnectRequestHandler() = default;

protected:
    Biz::ProjectInteractor& m_project_interactor;
    std::vector<BrowserLogicCommand> handle_message(const std::string& message);
    std::vector<BrowserLogicCommand> resend_config();

    // action callbacks stored in m_actions
    virtual std::vector<BrowserLogicCommand> on_connect_action_log(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_error(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_request_login(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_request_config(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_request_open_in_browser(const std::string& message_data);
    virtual std::vector<BrowserLogicCommand> on_connect_action_select_printer(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_print(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_webapp_ready(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_close_dialog(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_webview_reload_event(const std::string& message_data) = 0;
    virtual std::vector<BrowserLogicCommand> on_connect_action_log_in_in_browser(const std::string& message_data) = 0;

    std::map<std::string, std::function<std::vector<BrowserLogicCommand>(const std::string&)>> m_actions;
};

} // namespace Slic3r::App::Browser
