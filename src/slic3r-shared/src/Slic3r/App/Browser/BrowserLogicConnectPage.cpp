#include "Slic3r/App/Browser/BrowserLogicConnectPage.hpp"

#include "Slic3r/App/Browser/BrowserLogicLogInRedirect.hpp"
#include "Slic3r/App/Browser/BrowserCookie.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/JsonForScript.hpp"
#include <Slic3r/App/IDialogManager.hpp>
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Connect/ConnectMessageHandler.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::App::Browser {

namespace {
const std::string UNSUPPORTED_PAGE = "connect_unsupported";

bool offline()
{
    return Biz::Network::ServiceConfig::instance().is_offline();
}
} // namespace

BrowserLogicConnectPage::BrowserLogicConnectPage(Biz::ProjectInteractor& project_interactor) :
    AbstractBrowserLogic(
        Biz::Network::ServiceConfig::instance().connect_page_url(),
        {"_prusaSlicer"},
        "connect_loading",
        "connect_error"
    ),
    AbstractConnectRequestHandler(project_interactor, *this)
{}

void BrowserLogicConnectPage::refresh_url()
{
    // The server changes with the service mode, the tab stays. Online the URL never changes.
    std::string url = Biz::Network::ServiceConfig::instance().connect_page_url();
    if (url != m_url) {
        m_url                 = std::move(url);
        m_reached_default_url = false;
        m_styles_defined      = false;
    }
}

bool BrowserLogicConnectPage::is_connect_url(const std::string& url) const
{
    return !m_url.empty() && url.find(m_url) == 0;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::home_page_commands()
{
    std::vector<BrowserLogicCommand> res;
    if (m_project_interactor.user_account_interactor().is_logged_in()) {
        emplace_load_default_url_commands(res);
    } else {
        emplace_load_logged_out_page_commands(res);
    }
    return res;
}

std::string BrowserLogicConnectPage::access_token()
{
    return m_project_interactor.user_account_interactor().access_token();
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_script_message_webview_event(const std::string& message) 
{
     return handle_message(message);
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_webview_created()
{
    // Offline the page asks for its config itself and runs on a page session cookie.
    if (!m_project_interactor.user_account_interactor().is_logged_in() || offline()) {
        return {};
    }
    std::vector<BrowserLogicCommand> result;
    std::string javascript = get_login_script(true, m_project_interactor.user_account_interactor().access_token());
    result.emplace_back(BrowserLogicCommandType::RunScript, javascript);
    std::vector<BrowserLogicCommand> temp = resend_config();
    result.insert(result.end(), temp.begin(), temp.end());
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_show_webview_event(bool show)
{
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::RunScript, "window.location.reload();");
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_loaded_webview_event(const std::string& url)
{
    if (url.empty())
        return {};

    std::vector<BrowserLogicCommand> result;
    if (url.starts_with("file:")
        && url.find("/web/" + UNSUPPORTED_PAGE) != std::string::npos
        && offline())
    {
        result.emplace_back(BrowserLogicCommandType::RunScript, unsupported_offline_script());
    }

    if (is_connect_url(url)) {
        if (m_unsupported_engine) {
            emplace_load_unsupported_page_commands(result);
            return result;
        }
        emplace_define_css_commands(result);
    } else {
        m_styles_defined = false;
    }

    //m_load_default_url_on_next_error = false;

    if (url.find("/web/" + m_loading_html) != std::string::npos && m_load_default_url) {
        m_load_default_url = false;
        std::vector<BrowserLogicCommand> home = home_page_commands();
        result.insert(result.end(), home.begin(), home.end());
    }

    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url)
{
    // we need to do this to redefine css when reload is hit
    if (is_connect_url(new_url) && new_url == current_url) {
        m_styles_defined = false;
    }
    on_page_navigation();
    if (!m_url.empty() && new_url == m_url) {
        m_reached_default_url = true;
        return {};
    }
    if (new_url.find("connection_failed.html") != std::string::npos) {
        return {};
    }
    // A page of the slicer's own resources that this logic asked for is not leaving Connect:
    // the lock keeps the page from navigating elsewhere, not the slicer from showing its own
    // logged-out or setup page.
    if (offline()
        && m_own_resource_page
        && new_url.starts_with("file:")
        && new_url.find("/web/") != std::string::npos)
    {
        m_own_resource_page = false;
        return {};
    }
    if (m_reached_default_url && !is_connect_url(new_url)) {
        SPDLOG_INFO( "{} does not start with default url. Vetoing.", new_url);
        return {{BrowserLogicCommandType::Veto, std::string()}};
    } else if (m_reached_default_url && new_url.find("/web/" + m_loading_html) != std::string::npos) {
        return {{BrowserLogicCommandType::Veto, std::string()}};
    }
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_load_default_url()
{
    refresh_url();
    return home_page_commands();
}

void BrowserLogicConnectPage::emplace_load_default_url_commands(std::vector<BrowserLogicCommand>& res)
{
    refresh_url();
    m_styles_defined = false;

    if (offline()) {
        // The page session cookie goes into the store first, the page never gets the credential.
        // Without a session the page loads anyway and asks again (REQUEST_CONFIG).
        with_page_session({{BrowserLogicCommandType::LoadURL, m_url}}, OnSessionFailure::Then);
        return;
    }
    res.emplace_back(BrowserLogicCommandType::LoadURL, m_url);
}

void BrowserLogicConnectPage::emplace_load_logged_out_page_commands(std::vector<BrowserLogicCommand>& res)
{
    refresh_url();
    m_logged_out = true;
    m_styles_defined    = false;
    m_own_resource_page = true;
    res.emplace_back(BrowserLogicCommandType::LoadResourcesPage, "connect_logged_out");
}

void BrowserLogicConnectPage::emplace_load_unsupported_page_commands(std::vector<BrowserLogicCommand>& res)
{
    m_styles_defined = false;
    m_unsupported_engine = true;
    m_own_resource_page  = true;
    res.emplace_back(BrowserLogicCommandType::LoadResourcesPage, UNSUPPORTED_PAGE);
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_page_will_load_webview_event()
{
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::AddUserScript, get_engine_check_script());
    if (!m_project_interactor.user_account_interactor().is_logged_in() || offline()) {
        // Offline nothing carries the credential into the page.
        return result;
    }
    result.emplace_back(BrowserLogicCommandType::AddUserScript, get_login_script(false, m_project_interactor.user_account_interactor().access_token()));
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_user_account_id_success(bool is_refresh, const std::string& current_url)
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

    if (offline()) {
        // The page keeps its page session; a new credential means a new page load anyway.
        return {};
    }
    DEBUG_ASSERT(!m_project_interactor.user_account_interactor().access_token().empty());
    std::string javascript = get_login_script(true, m_project_interactor.user_account_interactor().access_token());
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::RunScript, javascript);
    std::vector<BrowserLogicCommand> temp = resend_config();
    result.insert(result.end(), temp.begin(), temp.end());
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_user_account_logged_out(const std::string& current_url)
{
    if (m_load_default_url) {
        return {};
    }
    m_unsupported_engine = false;
    std::vector<BrowserLogicCommand> res;
    if (!offline()) {
        res.emplace_back(BrowserLogicCommandType::RunScript, get_logout_script());
    } else {
        // Nothing in the page to sign out: it never had the credential, only the page session,
        // and a script there would make the server page reload itself over the signed-out page.
        // The page session dies with the credential on the server anyway.
        res.emplace_back(
            BrowserLogicCommandType::DeleteCookie,
            cookie_command_data(
                CONNECT_LOCAL_SESSION_COOKIE,
                Biz::Network::ServiceConfig::instance().offline_server_url()
            )
        );
    }
    emplace_load_logged_out_page_commands(res);
    return res; 
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::signed_out_commands()
{
    // A server page still shown after the sign-out would wait for a session forever.
    std::vector<BrowserLogicCommand> res;
    emplace_load_logged_out_page_commands(res);
    return res;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::logout()
{
    if (m_load_default_url) {
        return {};
    }
    if (offline()) {
        // Connect Local pages never get the credential, the logged-out handler loads the
        // signed-out page.
        return {};
    }
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::RunScript, "window._prusaConnect_v2.logout()");

    std::string access_token = m_project_interactor.user_account_interactor().access_token();
    std::string javascript = std::string(R"(
        console.log('Preparing logout');
        window.fetch('/slicer/logout', {method: 'POST', headers: {Authorization: 'Bearer )") + access_token + std::string(R"('}})
            .then(function (resp){
                console.log('Logout resp', resp);
                resp.text().then(function (json) { console.log('Logout resp body', json) });
            });
        )");
    result.emplace_back(BrowserLogicCommandType::RunScript, std::move(javascript));
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_select_printer(const std::string& message_data)
{
    m_project_interactor.connect_message_handler().handle_select_printer_message(message_data);
    return {{BrowserLogicCommandType::SwitchToSlicing, {}}};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_print(const std::string& message_data)
{
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_webapp_ready(const std::string& message_data)
{
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_webview_reload_event(const std::string& message_data)
{
     // Event from our error page button or keyboard shortcut 
    m_styles_defined = false;
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

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_close_dialog(const std::string& message_data)
{
    DEBUG_ASSERT(false, "Closing not supported in Panel.");
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_request_login(const std::string &message_data)
{
    if (!m_project_interactor.user_account_interactor().is_logged_in()) {
        std::vector<BrowserLogicCommand> res;
        emplace_load_logged_out_page_commands(res);
        return res;
    }

    if (offline()) {
        // Answered by REQUEST_CONFIG with a page session, never with the credential.
        return {};
    }
    return{{BrowserLogicCommandType::RunScript, get_login_script(true,  m_project_interactor.user_account_interactor().access_token())}};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_log_in_in_browser(const std::string& message_data)
{
    if (!m_project_interactor.user_account_interactor().is_logged_in()) {
        show_log_in_dialog(m_project_interactor);
    }
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_error(const std::string &message_data)
{
    SPDLOG_ERROR("WebView runtime error: {}", message_data);
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_unsupported_webview(const std::string& message_data)
{
    SPDLOG_ERROR("WebView engine does not support Connect web app: {}", message_data);
    // Deferred to on_loaded_webview_event, navigating away mid-load would cancel both loads.
    m_unsupported_engine = true;
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::on_connect_action_open_connect_in_browser(const std::string& message_data)
{
    if (m_url.empty()) {
        return {};
    }
    return {{BrowserLogicCommandType::OpenExternalBrowser, m_url}};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::page_session_failed_commands()
{
    m_styles_defined    = false;
    m_own_resource_page = true;
    return AbstractConnectRequestHandler::page_session_failed_commands();
}

std::string BrowserLogicConnectPage::unsupported_offline_script() const
{
    // connect_unsupported is shared with the online mode and names Prusa Connect.
    const nlohmann::json texts = {
        Biz::_u8L("Prusa Connect (on-premise) can't be displayed here."),
        Biz::_u8L(
            "Your system's built-in web browser is too old to run Prusa Connect (on-premise). Please install the latest updates for your operating system, then restart PrusaSlicer."
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

std::string BrowserLogicConnectPage::get_login_script(bool refresh, const std::string& access_token) const
{
    DEBUG_ASSERT(!access_token.empty());
    std::string javascript;

#if AUTH_VIA_FETCH_OVERRIDE
    if (refresh) {
        javascript = "window.__access_token = '" + access_token + "';window.__access_token_version = (window.__access_token_version || 0) + 1;console.log('Updated Auth token', window.__access_token);";
    } else {
        std::string javascript = std::string(R"(
            if (window.__fetch === undefined) {
                window.__fetch = fetch;
                window.fetch = function(req, opts = {}) {
                    if (typeof req === 'string') {
                        req = new Request(req, opts);
                        opts = {};
                    }
                    if (window.__access_token && (req.url[0] == '/' || req.url.indexOf('prusa3d.com') > 0)) {
                        req.headers.set('Authorization', 'Bearer ' + window.__access_token);
                        console.log('Header updated: ', req.headers.get('Authorization'));
                        console.log('AT Version: ', __access_token_version);
                    }
                    //console.log('Injected fetch used', req, opts);
                    return __fetch(req, opts);
                };
            }
            window.__access_token = ')")
            + access_token 
            + std::string(R"(';
            window.__access_token_version = 0;
            )");
    }
#else // !AUTH_VIA_FETCH_OVERRIDE
    if (refresh) {
        javascript = std::string(R"(
            if ()")
            + connect_page_js_condition()
            + std::string(R"() {
                if (window._prusaSlicer_initLogin !== undefined) {
                    console.log('Init login');
                    if (window._prusaSlicer !== undefined)
                        _prusaSlicer.postMessage({action: 'LOG', message: 'Refreshing login'});
                    _prusaSlicer_initLogin(')")
            + access_token
            + std::string(R"(');
                } else {
                    console.log('Refreshing login skipped as no _prusaSlicer_login defined (yet?)');
                    if (window._prusaSlicer === undefined) {
                        console.log('Message handler _prusaSlicer not defined yet');
                    } else {
                        _prusaSlicer.postMessage({action: 'LOG', message: 'Refreshing login skipped as no _prusaSlicer_initLogin defined (yet?)'});
                    }
                }
            }
            )");
    } else {
        javascript = R"(
            function _prusaSlicer_log(msg) {
                console.log(msg);
                if (window._prusaSlicer !== undefined)
                    _prusaSlicer.postMessage({action: 'LOG', message: msg});
            }
            function _prusaSlicer_errorHandler(err) {
                const msg = {
                    action: 'ERROR',
                    error: typeof(err) === 'string' ? err : JSON.stringify(err),
                    critical: false
                };
                console.error('Login error occurred', msg);
                window._prusaSlicer.postMessage(msg);
            };

            function _prusaSlicer_delay(ms) {
                return new Promise((resolve, reject) => {
                    setTimeout(resolve, ms);
                });
            }

            async function _prusaSlicer_initLogin(token) {
                const parts = token.split('.');
                const claims = JSON.parse(atob(parts[1]));
                const now = new Date().getTime() / 1000;
                if (claims.exp <= now) {
                    _prusaSlicer_log('Skipping initLogin as token is expired');
                    return;
                }

                let retry = false;
                let backoff = 1000;
                const maxBackoff = 64000 * 4;
                const maxRetries = 16;
                let numRetries = 0;
                do {

                    let error = false;

                    try {
                        _prusaSlicer_log('Slicer Login request ' + token.substring(token.length - 8));
                        let resp = await fetch('/slicer/login', {method: 'POST', headers: {Authorization: 'Bearer ' + token}});
                        let body = await resp.text();
                        _prusaSlicer_log('Slicer Login resp ' + resp.status + ' (' + token.substring(token.length - 8) + ') body: ' + body);
                        if (resp.status >= 500 || resp.status == 408) {
                            numRetries++;
                            retry = maxRetries <= 0 || numRetries <= maxRetries;
                        } else {
                            retry = false;
                            if (resp.status >= 400)
                                _prusaSlicer_errorHandler({status: resp.status, body});
                        }
                    } catch (e) {
                        _prusaSlicer_log('Slicer Login failed: ' + e.toString());
                        console.error('Slicer Login failed', e.toString());
                        // intentionally not taking care about max retry count, as this is not server error but likely being offline
                        retry = true;
                    }

                    if (retry) {
                        await _prusaSlicer_delay(backoff + 1000 * Math.random());
                        if (backoff < maxBackoff) {
                            backoff *= 2;
                        }
                    }
                } while (retry);
            }

            if ()"
            + connect_page_js_condition()
            + R"( && window._prusaSlicer) {
                _prusaSlicer_log('Requesting login');
                _prusaSlicer.postMessage({action: 'REQUEST_LOGIN'});
            }
        )";
    }
#endif
    return javascript;
}

std::string BrowserLogicConnectPage::get_logout_script() const
{
    return "sessionStorage.removeItem('_slicer_token');";
}

void BrowserLogicConnectPage::emplace_define_css_commands(std::vector<BrowserLogicCommand>& res)
{
     if (m_styles_defined) {
        return;
    }
    m_styles_defined = true;

#if defined(__APPLE__) 
    // WebView on Windows does read keyboard shortcuts
    // Thus doing f.e. Reload twice would make the oparation to fail
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
#endif // defined(__APPLE__)
}

} // namespace Slic3r::App::Browser 