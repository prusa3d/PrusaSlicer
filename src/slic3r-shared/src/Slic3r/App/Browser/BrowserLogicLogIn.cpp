#include "Slic3r/App/Browser/BrowserLogicLogIn.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

namespace Slic3r::App::Browser {


BrowserLogicLogIn::BrowserLogicLogIn(const std::string& url, Biz::UserAccount::UserAccountInteractor& user_account)
    : AbstractBrowserLogic(url, {})
    , m_user_account(user_account)
{
    set_title("Sign in");
}
    
std::vector<BrowserLogicCommand> BrowserLogicLogIn::on_navigation_request_webview_event(const std::string& url, const std::string& current_url)
{
   
    if (url.find("prusaslicer") == 0) {
        m_user_account.on_log_in_code_response(url);

        return {{BrowserLogicCommandType::DeleteCookiesWithCounter, Biz::Network::ServiceConfig::instance().account_url()},
            {BrowserLogicCommandType::DeleteCookiesWithCounter, "https://accounts.google.com"},
            {BrowserLogicCommandType::DeleteCookiesWithCounter, "https://appleid.apple.com"},
            {BrowserLogicCommandType::DeleteCookiesWithCounter, "https://facebook.com"}};
    }
    
    if (url.find("accounts.google.com") != std::string::npos
        || url.find("appleid.apple.com") != std::string::npos
        || url.find("facebook.com") != std::string::npos)
    {
        // TODO: open external browser
    }
    return {};
}

} // namespace Slic3r::App::Browser 