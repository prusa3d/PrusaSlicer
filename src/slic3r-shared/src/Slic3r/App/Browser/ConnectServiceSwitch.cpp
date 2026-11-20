#include "Slic3r/App/Browser/ConnectServiceSwitch.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AccountServiceSwitcher.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::App::Browser {

void ConnectServiceSwitch::add_texts(nlohmann::json& texts)
{
    texts["modeCloud"] = Biz::_u8L("Prusa Connect");
    texts["modeLocal"] = Biz::_u8L("Prusa Connect on-premise");
    // TRN Small label under the PRUSA CONNECT logo on the Prusa Connect on-premise pages.
    texts["modeTag"] = Biz::_u8L("on-premise");
    // TRN Accessible name of the switch between Prusa Connect and Prusa Connect on-premise.
    texts["modeSwitch"] = Biz::_u8L("Connect service");
}

bool ConnectServiceSwitch::shown()
{
    return AppServices::instance().app_config().are_both_account_services_enabled();
}

void ConnectServiceSwitch::add_actions(Actions& actions)
{
    actions["SET_ACCOUNT_SERVICE_PRUSA_ACCOUNT"] = [this](const std::string&)
    { return choose(AccountService::PrusaAccount); };
    actions["SET_ACCOUNT_SERVICE_CONNECT_LOCAL"] = [this](const std::string&)
    { return choose(AccountService::ConnectLocal); };
}

void ConnectServiceSwitch::on_page_loaded(const std::string& url)
{
    m_own_page = url.starts_with("file:");
}

void ConnectServiceSwitch::on_navigation_request(const std::string& new_url)
{
    m_own_page = m_own_page && new_url.starts_with("file:");
}

std::vector<BrowserLogicCommand> ConnectServiceSwitch::choose(AccountService service) const
{
    if (!m_own_page) {
        SPDLOG_WARN("Ignoring an account service change from a page that is not ours.");
        return {};
    }
    if (!AppServices::instance().account_service_switcher().choose(service)) {
        return {{BrowserLogicCommandType::DoReload, std::string()}};
    }
    return {};
}

} // namespace Slic3r::App::Browser
