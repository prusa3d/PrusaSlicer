#include "Slic3r/App/Browser/AbstractConnectPageLogic.hpp"

#include "Slic3r/App/Browser/BrowserLogicConnectLocalPage.hpp"
#include "Slic3r/App/Browser/BrowserLogicConnectPage.hpp"
#include "Slic3r/App/Browser/BrowserLogicLogInRedirect.hpp"
#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace Slic3r::App::Browser {

AbstractConnectPageLogic::AbstractConnectPageLogic(
    Biz::ProjectInteractor& project_interactor,
    const std::string& connect_page_url,
    std::unique_ptr<IConnectPageAuth> page_auth
) :
    AbstractBrowserLogic(connect_page_url, {"_prusaSlicer"}, LOADING_PAGE, ERROR_PAGE),
    AbstractConnectRequestHandler(
        project_interactor,
        std::move(page_auth),
        [this](std::vector<BrowserLogicCommand> commands) { push_commands(std::move(commands)); },
        ERROR_PAGE
    ),
    m_navigation_lock(connect_page_url, LOADING_PAGE)
{}

bool AbstractConnectPageLogic::is_connect_url(const std::string& url) const
{
    return m_navigation_lock.is_connect_url(url);
}

std::vector<BrowserLogicCommand> AbstractConnectPageLogic::home_page_commands()
{
    std::vector<BrowserLogicCommand> res;
    const std::string next_url = std::exchange(m_next_show_url, {});
    if (std::optional<std::vector<BrowserLogicCommand>> requested = show_request_commands(next_url)) {
        return std::move(*requested);
    }
    if (m_project_interactor.user_account_interactor().is_logged_in()) {
        emplace_load_default_url_commands(res, next_url);
    } else {
        emplace_load_logged_out_page_commands(res);
    }
    return res;
}

std::string AbstractConnectPageLogic::access_token()
{
    return m_project_interactor.user_account_interactor().access_token();
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_script_message_webview_event(const std::string& message)
{
    return handle_message(message);
}

std::vector<BrowserLogicCommand> AbstractConnectPageLogic::on_webview_created()
{
    if (!m_project_interactor.user_account_interactor().is_logged_in()) {
        return {};
    }
    return signed_in_page_commands();
}

std::vector<BrowserLogicCommand> AbstractConnectPageLogic::on_show_webview_event(bool show)
{
    if (!m_next_show_url.empty()) {
        return home_page_commands();
    }
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::RunScript, "window.location.reload();");
    return result;
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_loaded_webview_event(const std::string& url)
{
    if (url.empty()) {
        return {};
    }

    std::vector<BrowserLogicCommand> result = page_loaded_commands(url);

    if (is_connect_url(url)) {
        if (m_unsupported_engine) {
            emplace_load_unsupported_page_commands(result);
            return result;
        }
        emplace_define_css_commands(result);
    } else {
        m_styles_defined = false;
    }

    if (url.find("/web/" + m_loading_html) != std::string::npos && m_load_default_url) {
        m_load_default_url = false;
        std::vector<BrowserLogicCommand> home = home_page_commands();
        result.insert(result.end(), home.begin(), home.end());
    }

    return result;
}

std::vector<BrowserLogicCommand> AbstractConnectPageLogic::
    on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url)
{
    on_navigating(new_url);
    if (is_connect_url(new_url) && new_url == current_url) {
        m_styles_defined = false;
    }
    on_page_navigation();
    if (!m_navigation_lock.allows(new_url, app_pages_pass_navigation_lock())) {
        SPDLOG_INFO("{} is not a page of the Connect tab's service. Vetoing.", new_url);
        return {{BrowserLogicCommandType::Veto, std::string()}};
    }
    return {};
}

std::vector<BrowserLogicCommand> AbstractConnectPageLogic::on_load_default_url()
{
    return home_page_commands();
}

void AbstractConnectPageLogic::emplace_load_default_url_commands(
    std::vector<BrowserLogicCommand>& res,
    const std::string& page_url
)
{
    m_styles_defined = false;
    emplace_load_connect_page_commands(res, is_connect_url(page_url) ? page_url : m_url);
}

void AbstractConnectPageLogic::
    emplace_load_logged_out_page_commands(std::vector<BrowserLogicCommand>& res)
{
    m_logged_out = true;
    emplace_logged_out_page_commands(res);
}

void AbstractConnectPageLogic::emplace_load_resources_page_commands(
    std::vector<BrowserLogicCommand>& res,
    const std::string& page
)
{
    m_styles_defined = false;
    res.emplace_back(BrowserLogicCommandType::LoadResourcesPage, page);
}

void AbstractConnectPageLogic::
    emplace_load_unsupported_page_commands(std::vector<BrowserLogicCommand>& res)
{
    m_unsupported_engine = true;
    emplace_load_resources_page_commands(res, UNSUPPORTED_PAGE);
}

std::vector<BrowserLogicCommand> AbstractConnectPageLogic::on_page_will_load_webview_event()
{
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::AddUserScript, get_engine_check_script());
    if (!m_project_interactor.user_account_interactor().is_logged_in()) {
        return result;
    }
    std::vector<BrowserLogicCommand> scripts = page_user_script_commands();
    result.insert(result.end(), scripts.begin(), scripts.end());
    return result;
}

std::vector<BrowserLogicCommand> AbstractConnectPageLogic::
    on_user_account_id_success(bool is_refresh, const std::string& current_url)
{
    if (m_load_default_url || m_unsupported_engine) {
        return {};
    }
    if (m_logged_out) {
        m_logged_out = false;
        std::vector<BrowserLogicCommand> res;
        emplace_load_default_url_commands(res);
        return res;
    }
    return signed_in_page_commands();
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_user_account_logged_out(const std::string& current_url)
{
    if (m_load_default_url) {
        return {};
    }
    m_unsupported_engine = false;
    std::vector<BrowserLogicCommand> res = signed_out_cleanup_commands();
    emplace_load_logged_out_page_commands(res);
    return res;
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_select_printer(const std::string& message_data)
{
    m_project_interactor.connect_message_handler().handle_select_printer_message(message_data);
    return {{BrowserLogicCommandType::SwitchToSlicing, {}}};
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_print(const std::string& message_data)
{
    return {};
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_webapp_ready(const std::string& message_data)
{
    return {};
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_webview_reload_event(const std::string& message_data)
{
    m_styles_defined     = false;
    m_unsupported_engine = false;
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("fromKeyboard") && j["fromKeyboard"].is_boolean() && j["fromKeyboard"].get<bool>()) {
            return {{BrowserLogicCommandType::DoReload, {}}};
        } else {
            return home_page_commands();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Connect message. {}", e.what());
        return {};
    }
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_close_dialog(const std::string& message_data)
{
    DEBUG_ASSERT(false, "Closing not supported in Panel.");
    return {};
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_request_login(const std::string& message_data)
{
    if (!m_project_interactor.user_account_interactor().is_logged_in()) {
        std::vector<BrowserLogicCommand> res;
        emplace_load_logged_out_page_commands(res);
        return res;
    }
    return sign_in_refresh_commands();
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_log_in_in_browser(const std::string& message_data)
{
    if (!m_project_interactor.user_account_interactor().is_logged_in()) {
        show_log_in_dialog(m_project_interactor);
    }
    return {};
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_error(const std::string& message_data)
{
    SPDLOG_ERROR("WebView runtime error: {}", message_data);
    return {};
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_unsupported_webview(const std::string& message_data)
{
    SPDLOG_ERROR("WebView engine does not support Connect web app: {}", message_data);
    m_unsupported_engine = true;
    return {};
}

std::vector<BrowserLogicCommand>
AbstractConnectPageLogic::on_connect_action_open_connect_in_browser(const std::string& message_data)
{
    if (m_url.empty()) {
        return {};
    }
    return {{BrowserLogicCommandType::OpenExternalBrowser, m_url}};
}

void AbstractConnectPageLogic::emplace_define_css_commands(std::vector<BrowserLogicCommand>& res)
{
    if (m_styles_defined) {
        return;
    }
    m_styles_defined = true;

#if defined(__APPLE__)
    std::string script = R"(
        document.addEventListener('keydown', function (event) {
            if (event.key === 'F5' || (event.ctrlKey && event.key === 'r') || (event.metaKey && event.key === 'r')) {
                 window.webkit.messageHandlers._prusaSlicer.postMessage(JSON.stringify({ action: 'reloadHomePage', fromKeyboard: 1}));
            }
            if (event.metaKey && event.key === 'q') {
                 window.webkit.messageHandlers._prusaSlicer.postMessage(JSON.stringify({ action: 'appQuit'}));
            }
            if (event.metaKey && event.key === 'm') {
                 window.webkit.messageHandlers._prusaSlicer.postMessage(JSON.stringify({ action: 'appMinimize'}));
            }
        });
    )";
    res.emplace_back(BrowserLogicCommandType::RunScript, script);
#endif
}

std::unique_ptr<AbstractBrowserLogic>
make_connect_page_logic(Biz::ProjectInteractor& project_interactor)
{
    if (Biz::Network::ServiceConfig::instance().is_connect_local()) {
        return std::make_unique<BrowserLogicConnectLocalPage>(project_interactor);
    }
    return std::make_unique<BrowserLogicConnectPage>(project_interactor);
}

} // namespace Slic3r::App::Browser
