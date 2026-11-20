#include "Slic3r/App/Browser/BrowserLogicConnectLocalPage.hpp"

#include "Slic3r/App/Browser/BrowserCookie.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/JsonForScript.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace Slic3r::App::Browser {

BrowserLogicConnectLocalPage::BrowserLogicConnectLocalPage(
    Biz::ProjectInteractor& project_interactor
) :
    AbstractConnectPageLogic(
        project_interactor,
        Biz::Network::ServiceConfig::instance().connect_page_url(),
        make_connect_page_session_auth(project_interactor.user_account_interactor())
    ),
    m_server_url(Biz::Network::ServiceConfig::instance().connect_local_server_url()),
    m_setup(
        project_interactor.user_account_interactor(),
        {
            .push_commands = [this](std::vector<BrowserLogicCommand> commands)
            { push_commands(std::move(commands)); },
            .setup_page =
                [this]()
            {
                std::vector<BrowserLogicCommand> res;
                emplace_load_setup_page_commands(res);
                return res;
            },
            .home_page = [this]() { return home_page_commands(); },
            .logged_out_page =
                [this]()
            {
                std::vector<BrowserLogicCommand> res;
                emplace_load_logged_out_page_commands(res);
                return res;
            },
        }
    )
{
    m_setup.add_actions(m_actions);
}

BrowserLogicConnectLocalPage::~BrowserLogicConnectLocalPage() = default;

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalPage::on_show_webview_event(bool show)
{
    m_setup.set_shown(show);
    return AbstractConnectPageLogic::on_show_webview_event(show);
}

void BrowserLogicConnectLocalPage::on_hide_webview_event()
{
    m_setup.set_shown(false);
}

void BrowserLogicConnectLocalPage::on_visible_webview_event()
{
    m_setup.set_shown(true);
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectLocalPage::on_load_error(const std::string& url, bool certificate_error)
{
    if (!certificate_error) {
        return {};
    }
    return m_setup.on_certificate_error(url);
}

void BrowserLogicConnectLocalPage::emplace_load_connect_page_commands(
    std::vector<BrowserLogicCommand>& res,
    const std::string& url
)
{
    if (ConnectLocalSetupLogic::needs_server_setup()) {
        emplace_load_setup_page_commands(res);
        return;
    }
    with_page_session({{BrowserLogicCommandType::LoadURL, url}}, OnSessionFailure::Then);
}

void BrowserLogicConnectLocalPage::
    emplace_logged_out_page_commands(std::vector<BrowserLogicCommand>& res)
{
    if (m_setup.wants_setup_page()) {
        emplace_load_setup_page_commands(res);
        return;
    }
    emplace_load_resources_page_commands(
        res,
        std::string(ConnectLocalSetupLogic::LOGGED_OUT_PAGE)
    );
}

void BrowserLogicConnectLocalPage::
    emplace_load_setup_page_commands(std::vector<BrowserLogicCommand>& res)
{
    emplace_load_resources_page_commands(res, std::string(ConnectLocalSetupLogic::SETUP_PAGE));
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalPage::page_user_script_commands()
{
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalPage::sign_in_refresh_commands()
{
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalPage::signed_in_page_commands()
{
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalPage::signed_out_cleanup_commands()
{
    return {
        {BrowserLogicCommandType::DeleteCookie,
         cookie_command_data(CONNECT_LOCAL_SESSION_COOKIE, m_server_url)}
    };
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectLocalPage::page_loaded_commands(const std::string& url)
{
    std::vector<BrowserLogicCommand> result = m_setup.on_page_loaded(url);
    if (url.starts_with("file:")
        && url.find(std::string("/web/") + UNSUPPORTED_PAGE) != std::string::npos)
    {
        result.emplace_back(BrowserLogicCommandType::RunScript, unsupported_page_script());
    }
    return result;
}

void BrowserLogicConnectLocalPage::on_navigating(const std::string& new_url)
{
    m_setup.on_navigation_request(new_url);
}

std::optional<std::vector<BrowserLogicCommand>>
BrowserLogicConnectLocalPage::show_request_commands(const std::string& request)
{
    if (request != ConnectLocalSetupLogic::SETUP_REQUEST
        || m_project_interactor.user_account_interactor().is_logged_in())
    {
        return std::nullopt;
    }
    m_setup.request_setup();
    std::vector<BrowserLogicCommand> res;
    emplace_load_logged_out_page_commands(res);
    return res;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalPage::page_session_failed_commands()
{
    std::vector<BrowserLogicCommand> res;
    emplace_load_resources_page_commands(res, ERROR_PAGE);
    return res;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalPage::signed_out_commands()
{
    std::vector<BrowserLogicCommand> res;
    emplace_load_logged_out_page_commands(res);
    return res;
}

std::string BrowserLogicConnectLocalPage::unsupported_page_script() const
{
    const nlohmann::json texts = {
        Biz::_u8L("Prusa Connect on-premise can't be displayed here."),
        Biz::_u8L(
            "Your system's built-in web browser is too old to run Prusa Connect on-premise. Please install the latest updates for your operating system, then restart PrusaSlicer."
        ),
    };
    return std::string(R"(
        (function(texts) {
            var paragraphs = document.querySelectorAll('#loading-screen p');
            var target = [];
            paragraphs.forEach(function(p) { if (p.querySelector('svg') === null) target.push(p); });
            for (var i = 0; i < texts.length && i < target.length; ++i) target[i].textContent = texts[i];
        })()")
        + Biz::json_for_script(texts)
        + ");";
}

} // namespace Slic3r::App::Browser
