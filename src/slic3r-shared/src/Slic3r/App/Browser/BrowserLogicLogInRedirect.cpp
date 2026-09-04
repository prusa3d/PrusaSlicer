#include "Slic3r/App/Browser/BrowserLogicLogInRedirect.hpp"

#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::App::Browser {

BrowserLogicLogInRedirect::BrowserLogicLogInRedirect(Biz::UserAccount::UserAccountInteractor& user_account) :
    AbstractBrowserLogic({}, {"PrusaSlicerWebviewMessage"}, "login_before_redirect"),
    m_user_account(user_account)
{
    set_title("Sign in");
}

std::vector<BrowserLogicCommand> BrowserLogicLogInRedirect::on_loaded_webview_event(const std::string& url)
{
    if (url.empty())
        return {};

    std::vector<BrowserLogicCommand> result;

    if (url.find("/web/" + m_loading_html) != std::string::npos && m_load_default_url) {
        m_load_default_url = false;
        m_redirect_url     = m_user_account.on_log_in_request("en", true);
        return {
            {BrowserLogicCommandType::RegisterPrusaSlicerURL, std::string()},
            {BrowserLogicCommandType::LoadResourcesPage, "login_redirected"},
            {BrowserLogicCommandType::OpenExternalBrowser, m_redirect_url}
        };
    }
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicLogInRedirect::on_script_message_webview_event(const std::string& message)
{
    try {
        nlohmann::json j = nlohmann::json::parse(message);
        if (j.contains("event") && j["event"].is_string() && j["event"].get<std::string>() == "reopenLogInBrowser")
        {
            return {{BrowserLogicCommandType::OpenExternalBrowser, m_redirect_url}};
        }
        if (j.contains("event") && j["event"].is_string() && j["event"].get<std::string>() == "openLoginPage")
        {
            return {{BrowserLogicCommandType::LoadURL, m_user_account.on_log_in_request("en", true)}};
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse WebView message. {}", e.what());
        return {};
    }
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicLogInRedirect::on_user_account_id_success(bool is_refresh, const std::string& current_url)
{
    return {{BrowserLogicCommandType::EndModalOK, {}}};
}

std::vector<BrowserLogicCommand>
BrowserLogicLogInRedirect::on_navigation_request_webview_event(const std::string& url, const std::string& current_url)
{
    if (url.find("prusaslicer") == 0) {
        m_user_account.on_log_in_code_response(url);

        return {
            {BrowserLogicCommandType::DeleteCookiesWithCounter,
             Biz::Network::ServiceConfig::instance().account_url()},
            {BrowserLogicCommandType::DeleteCookiesWithCounter, "https://accounts.google.com"},
            {BrowserLogicCommandType::DeleteCookiesWithCounter, "https://appleid.apple.com"},
            {BrowserLogicCommandType::DeleteCookiesWithCounter, "https://facebook.com"}
        };
    }

    if (url.find("accounts.google.com") != std::string::npos || url.find("appleid.apple.com") != std::string::npos || url.find("facebook.com") != std::string::npos)
    {
        return {
            {BrowserLogicCommandType::RegisterPrusaSlicerURL, std::string()}, 
            {BrowserLogicCommandType::OpenExternalBrowser, url}
        };
    }
    return {};
}

} // namespace Slic3r::App::Browser
