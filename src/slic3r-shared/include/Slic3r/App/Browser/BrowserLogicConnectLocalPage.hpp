#pragma once

#include "Slic3r/App/Browser/AbstractConnectPageLogic.hpp"
#include "Slic3r/App/Browser/ConnectLocalSetupLogic.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App::Browser {

/**
 * @brief The Connect tab showing a Prusa Connect Local server (Connect Local). The server's pages
 * run on a page session cookie and never get the credential. Without a usable server the tab
 * shows the server setup (ConnectLocalSetupLogic), which also handles a refused certificate.
 */
class BrowserLogicConnectLocalPage final : public AbstractConnectPageLogic
{
public:
    explicit BrowserLogicConnectLocalPage(Biz::ProjectInteractor& project_interactor);
    ~BrowserLogicConnectLocalPage() override;

    std::vector<BrowserLogicCommand> on_show_webview_event(bool show) override;
    void on_hide_webview_event() override;
    void on_visible_webview_event() override;
    std::vector<BrowserLogicCommand>
    on_load_error(const std::string& url, bool certificate_error) override;

protected:
    void emplace_load_connect_page_commands(
        std::vector<BrowserLogicCommand>& res,
        const std::string& url
    ) override;
    void emplace_logged_out_page_commands(std::vector<BrowserLogicCommand>& res) override;
    std::vector<BrowserLogicCommand> page_user_script_commands() override;
    std::vector<BrowserLogicCommand> sign_in_refresh_commands() override;
    std::vector<BrowserLogicCommand> signed_in_page_commands() override;
    std::vector<BrowserLogicCommand> signed_out_cleanup_commands() override;
    std::vector<BrowserLogicCommand> page_loaded_commands(const std::string& url) override;
    void on_navigating(const std::string& new_url) override;
    std::optional<std::vector<BrowserLogicCommand>>
    show_request_commands(const std::string& request) override;
    bool app_pages_pass_navigation_lock() const override
    {
        return true;
    }

    std::vector<BrowserLogicCommand> page_session_failed_commands() override;
    std::vector<BrowserLogicCommand> signed_out_commands() override;

private:
    void emplace_load_setup_page_commands(std::vector<BrowserLogicCommand>& res);
    std::string unsupported_page_script() const;

    std::string m_server_url;
    ConnectLocalSetupLogic m_setup;
};

} // namespace Slic3r::App::Browser
