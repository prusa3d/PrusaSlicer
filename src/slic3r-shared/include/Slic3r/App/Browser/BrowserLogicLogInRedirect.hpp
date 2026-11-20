#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"

#include <memory>

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::UserAccount {
    class UserAccountInteractor;
}

namespace Slic3r::App::Browser {

class BrowserLogicLogInRedirect final : public AbstractBrowserLogic {
public:
    BrowserLogicLogInRedirect(Biz::UserAccount::UserAccountInteractor& user_account);
    
    std::pair<int,int> size(int em_unit) const override {return std::pair<int,int>(50*em_unit, 80*em_unit); }
    std::vector<BrowserLogicCommand> on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url) override;
    std::vector<BrowserLogicCommand> on_loaded_webview_event(const std::string& url) override;
    std::vector<BrowserLogicCommand> on_script_message_webview_event(const std::string& message) override;
    std::vector<BrowserLogicCommand> on_user_account_id_success(bool is_refresh, const std::string& current_url) override;

private:
    Biz::UserAccount::UserAccountInteractor& m_user_account;

    bool m_load_default_url { true };
    std::string m_redirect_url;
};

/**
 * @brief The logic of the sign-in dialog: Prusa Account signs in in the browser (OAuth), Connect
 * Local (Connect Local) by app pairing, see BrowserLogicConnectLocalLogIn.
 */
std::unique_ptr<AbstractBrowserLogic>
make_log_in_logic(Biz::UserAccount::UserAccountInteractor& user_account);

/**
 * @brief Shows the sign-in dialog of make_log_in_logic(). Online the sign-in ends in the system
 * browser, so the app is brought back to the front; Connect Local it ends inside the dialog, and
 * raising would only restore and maximize the window again.
 */
void show_log_in_dialog(Biz::ProjectInteractor& project_interactor);

} // namespace Slic3r::App::Browser 