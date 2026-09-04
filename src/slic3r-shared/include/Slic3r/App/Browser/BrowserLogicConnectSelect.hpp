#pragma once

#include "Slic3r/App/Browser/AbstractUploadBrowserLogic.hpp"
#include "Slic3r/App/Browser/AbstractConnectRequestHandler.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
}
namespace Slic3r::App::Browser {

class BrowserLogicConnectSelect final : public AbstractUploadBrowserLogic, public AbstractConnectRequestHandler
{
public:
    BrowserLogicConnectSelect(Biz::ProjectInteractor& project_interactor);
    
    // AbstractBrowserLogic
    std::vector<BrowserLogicCommand> on_script_message_webview_event(const std::string& message) override;
    std::vector<BrowserLogicCommand> on_show_webview_event(bool show) override;
    std::pair<int,int> size(int em_unit) const override {return std::pair<int,int>(200*em_unit, 100*em_unit); }
    std::pair<int,int> min_size(int em_unit) const override { return {100 * em_unit, 50 * em_unit}; }
protected:
    // AbstractConnectRequestHandler
    std::vector<BrowserLogicCommand> on_connect_action_select_printer(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_print(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_webapp_ready(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> request_compatible_printers_FFF();
    std::vector<BrowserLogicCommand> request_compatible_printers_SLA();
    std::vector<BrowserLogicCommand> on_webview_reload_event(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_close_dialog(const std::string& message_data) override;
    std::vector<BrowserLogicCommand> on_connect_action_log_in_in_browser(const std::string& data) override;
};
} // namespace Slic3r::App::Browser 