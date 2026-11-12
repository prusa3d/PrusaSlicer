#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"

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
} // namespace Slic3r::App::Browser 