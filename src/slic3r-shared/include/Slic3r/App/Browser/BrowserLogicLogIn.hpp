#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"

namespace Slic3r::Biz::UserAccount {
    class UserAccountInteractor;
}

namespace Slic3r::App::Browser {

class BrowserLogicLogIn final : public AbstractBrowserLogic {
public:
    BrowserLogicLogIn(const std::string& url, Biz::UserAccount::UserAccountInteractor& user_account);
    
    std::pair<int,int> size(int em_unit) const override {return std::pair<int,int>(50*em_unit, 80*em_unit); }
    std::vector<BrowserLogicCommand> on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url) override;

private:
    Biz::UserAccount::UserAccountInteractor& m_user_account;
};
} // namespace Slic3r::App::Browser 