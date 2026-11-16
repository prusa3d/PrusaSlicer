#include "Slic3r/App/Browser/BrowserLogicConnectPage.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/JsonForScript.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Log.hpp"

#include <nlohmann/json.hpp>

#include <memory>

namespace Slic3r::App::Browser {

BrowserLogicConnectPage::BrowserLogicConnectPage(Biz::ProjectInteractor& project_interactor) :
    AbstractConnectPageLogic(
        project_interactor,
        Biz::Network::ServiceConfig::instance().connect_page_url(),
        std::make_unique<ConnectTokenAuth>()
    )
{
    m_service_switch.add_actions(m_actions);
}

void BrowserLogicConnectPage::emplace_load_connect_page_commands(
    std::vector<BrowserLogicCommand>& res,
    const std::string& url
)
{
    res.emplace_back(BrowserLogicCommandType::LoadURL, url);
}

void BrowserLogicConnectPage::emplace_logged_out_page_commands(std::vector<BrowserLogicCommand>& res)
{
    emplace_load_resources_page_commands(res, LOGGED_OUT_PAGE);
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::page_user_script_commands()
{
    return {
        {BrowserLogicCommandType::AddUserScript, get_login_script(false, access_token())}
    };
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::sign_in_refresh_commands()
{
    return {{BrowserLogicCommandType::RunScript, get_login_script(true, access_token())}};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::signed_in_page_commands()
{
    std::vector<BrowserLogicCommand> result = sign_in_refresh_commands();
    std::vector<BrowserLogicCommand> config = resend_config();
    result.insert(result.end(), config.begin(), config.end());
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicConnectPage::signed_out_cleanup_commands()
{
    return {{BrowserLogicCommandType::RunScript, get_logout_script()}};
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectPage::page_loaded_commands(const std::string& url)
{
    m_service_switch.on_page_loaded(url);
    if (m_service_switch.on_own_page()
        && url.find(std::string("/web/") + LOGGED_OUT_PAGE) != std::string::npos)
    {
        return {{BrowserLogicCommandType::RunScript, logged_out_page_init_script()}};
    }
    return {};
}

void BrowserLogicConnectPage::on_navigating(const std::string& new_url)
{
    m_service_switch.on_navigation_request(new_url);
}

std::string BrowserLogicConnectPage::logged_out_page_init_script() const
{
    nlohmann::json texts = {
        {"title", Biz::_u8L("Sign in to Prusa Connect")},
        {"intro",
         Biz::_u8L(
             "Sign in with your Prusa Account to send prints to your printers and see their status here."
         )},
        {"login", Biz::_u8L("Sign in")},
    };
    ConnectServiceSwitch::add_texts(texts);
    const nlohmann::json init = {{"texts", texts}, {"showSwitch", ConnectServiceSwitch::shown()}};
    return "window.connectLoggedOut && window.connectLoggedOut.init("
        + Biz::json_for_script(init)
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
            + page_auth().page_js_condition()
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
            + page_auth().page_js_condition()
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

} // namespace Slic3r::App::Browser
