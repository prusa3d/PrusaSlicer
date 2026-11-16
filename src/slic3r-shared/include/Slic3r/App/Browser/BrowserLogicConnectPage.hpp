#pragma once

#include "Slic3r/App/Browser/AbstractConnectPageLogic.hpp"
#include "Slic3r/App/Browser/ConnectServiceSwitch.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
}
namespace Slic3r::App::Browser {

/**
 * @brief The Connect tab showing Prusa Connect: the page gets the access token of the Prusa
 * Account through its config and a login script.
 */
class BrowserLogicConnectPage final : public AbstractConnectPageLogic
{
public:
    explicit BrowserLogicConnectPage(Biz::ProjectInteractor& project_interactor);

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

private:
    static constexpr const char* LOGGED_OUT_PAGE = "connect_logged_out";

    std::string logged_out_page_init_script() const;
    std::string get_login_script(bool refresh, const std::string& access_token) const;
    std::string get_logout_script() const;

    ConnectServiceSwitch m_service_switch;
};
} // namespace Slic3r::App::Browser
