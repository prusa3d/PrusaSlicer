#include "Slic3r/App/Browser/BrowserLogicPhysicalPrinter.hpp"

#include "Slic3r/Assert.hpp"
#include <Slic3r/Log.hpp>
#include "Slic3r/Biz/Network/IHttp.hpp"

#include <nlohmann/json.hpp>
#include <fmt/format.h>

namespace Slic3r::App::Browser {

namespace {
constexpr int MAX_AUTO_RETRIES = 3;
}

BrowserLogicPhysicalPrinter::BrowserLogicPhysicalPrinter(
    const std::string& url,
    const std::string& api_key,
    const std::string& usr,
    const std::string& psk)
    : AbstractBrowserLogic(url, {"ExternalApp"}, "other_loading", "other_error")
    , m_api_key(api_key)
    , m_usr(usr)
    , m_psk(psk)
    , m_base_url(Slic3r::Biz::Network::IHttp::get_base_url(url))
{
    m_events["reloadHomePage"] = [this](const std::string& message_data) { return on_reload_event(message_data); };
    m_events["appQuit"] = [this](const std::string& message_data) { return on_dummy_event(message_data); };
    m_events["appMinimize"] = [this](const std::string& message_data) { return on_dummy_event(message_data); };
}

std::vector<BrowserLogicCommand>
BrowserLogicPhysicalPrinter::on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url)
{
    SPDLOG_DEBUG("{} {}", __FUNCTION__, new_url);
    std::vector<BrowserLogicCommand> result;

    if (new_url.find(m_url) == 0) {
        m_reached_default_url = true;
        m_last_target_url = new_url;
        if (new_url == current_url) {
            m_styles_defined = false;
        }
        if (!m_api_key_sent && !m_usr.empty() && !m_psk.empty()) {
            auto cred_cmds = send_credentials();
            result.insert(result.end(), cred_cmds.begin(), cred_cmds.end());
        }
    } else if (m_reached_default_url && !m_load_default_url && new_url.find("/web/" + m_loading_html) != std::string::npos) {
        result.emplace_back(BrowserLogicCommandType::Veto, std::string());
    }

    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_loaded_webview_event(const std::string& url)
{
    SPDLOG_DEBUG("{} {}", __FUNCTION__, url);

    if (url.find("/web/" + m_loading_html) != std::string::npos && m_load_default_url) {
        return on_loading_html_loaded();
    }

    if (url.find("/web/" + m_error_html) != std::string::npos) {
        return {{BrowserLogicCommandType::SetLoadDefaultURLOnErrorFalse, std::string()}};
    }

    if (!m_base_url_preloaded && m_base_url != m_url && url.find(m_base_url) == 0 && url.find(m_url) != 0) {
        return on_base_url_loaded();
    }

    return on_target_url_loaded(url);
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_loading_html_loaded()
{
    m_load_default_url = false;
    std::vector<BrowserLogicCommand> result;

    // Credentials must be set before the base-URL SPA makes its init API calls.
    if (!m_api_key_sent && !m_usr.empty() && !m_psk.empty()) {
        auto cred_cmds = send_credentials();
        result.insert(result.end(), cred_cmds.begin(), cred_cmds.end());
    }
    result.emplace_back(BrowserLogicCommandType::LoadURL, m_base_url);
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_base_url_loaded()
{
    m_base_url_preloaded = true;
    m_reached_default_url = true;

    std::string path = "/";
    if (m_url.size() >= m_base_url.size() && m_url.compare(0, m_base_url.size(), m_base_url) == 0) {
        path = m_url.substr(m_base_url.size());
    }

    return {
        {BrowserLogicCommandType::RunScript, script_show_loading_overlay()},
        {BrowserLogicCommandType::RunScript, script_navigate_to_path(path)},
        {BrowserLogicCommandType::SetLoadDefaultURLOnErrorFalse, std::string()}
    };
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_target_url_loaded(const std::string& url)
{
    std::vector<BrowserLogicCommand> result;

    if (url.find(m_url) == 0) {
        emplace_define_css_commands(result);
        // First attempt: route an auth failure through on_load_default_url() instead of the error page.
        bool first_attempt = m_auto_retry_count == 0 && uses_basic_auth();
        result.emplace_back(first_attempt ? BrowserLogicCommandType::SetLoadDefaultURLOnErrorTrue
                                           : BrowserLogicCommandType::SetLoadDefaultURLOnErrorFalse,
                             std::string());
    } else {
        m_styles_defined = false;
        result.emplace_back(BrowserLogicCommandType::SetLoadDefaultURLOnErrorFalse, std::string());
    }

    if (!m_api_key.empty() && !m_api_key_sent) {
        auto key_cmds = send_api_key();
        result.insert(result.end(), key_cmds.begin(), key_cmds.end());
    }

    return result;
}

// The first request to m_url always fails (session cookie not yet accepted by the
// server), so retry silently here instead of letting WebViewPanel show the error page.
// Keep returning SetLoadDefaultURLOnErrorTrue in case a delayed error from the failed
// attempt arrives after this retry has already started.
std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_load_default_url()
{
    SPDLOG_DEBUG("{}", __FUNCTION__);
    if (m_connection_changed) {
        return reconnect_commands();
    }
    if (!m_last_target_url.empty() && m_auto_retry_count < MAX_AUTO_RETRIES && uses_basic_auth()) {
        ++m_auto_retry_count;
        return {
            {BrowserLogicCommandType::SetLoadDefaultURLOnErrorTrue, std::string()},
            {BrowserLogicCommandType::LoadURL, m_last_target_url}
        };
    }
    return {{BrowserLogicCommandType::LoadResourcesPage, m_error_html}};
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_show_webview_event(bool show)
{
    if (!show || !m_connection_changed) {
        return {};
    }
    return reconnect_commands();
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::reconnect_commands()
{
    m_connection_changed = false;
    return {
        {BrowserLogicCommandType::DeleteCookies, m_base_url},
        {BrowserLogicCommandType::ClearBasicAuth, std::string()},
        {BrowserLogicCommandType::LoadResourcesPage, m_loading_html}
    };
}

void BrowserLogicPhysicalPrinter::update_connection(
    const std::string& url, const std::string& api_key, const std::string& usr, const std::string& psk)
{
    if (url == m_url && api_key == m_api_key && usr == m_usr && psk == m_psk) {
        return;
    }

    m_url = url;
    m_api_key = api_key;
    m_usr = usr;
    m_psk = psk;
    m_base_url = Slic3r::Biz::Network::IHttp::get_base_url(url);

    m_last_target_url.clear();
    m_reached_default_url = false;
    m_styles_defined = false;
    m_api_key_sent = false;
    m_load_default_url = true;
    m_base_url_preloaded = false;
    m_auto_retry_count = 0;
    m_connection_changed = true;
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_script_message_webview_event(const std::string& message)
{
    SPDLOG_DEBUG("{}", __FUNCTION__);
    std::string event_string;
    try {
        nlohmann::json j = nlohmann::json::parse(message);
        if (!j.contains("event") || !j["event"].is_string()) {
            SPDLOG_ERROR("Received invalid message from Physical Printer (missing/invalid 'event'). Message: {}", message);
            return {};
        }
        event_string = j["event"].get<std::string>();
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Physical Printer message. {}", e.what());
        return {};
    }

    auto it = m_events.find(event_string);
    if (it == m_events.end()) {
        SPDLOG_WARN("Physical Printer Request has no handling function. Event: {}", event_string);
        return {};
    }
    return it->second(message);
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::send_api_key()
{
    SPDLOG_DEBUG("{}", __FUNCTION__);
    m_api_key_sent = true;

    std::string safe_api_key = nlohmann::json(m_api_key).dump();
    std::string script = fmt::format(R"(
        if (window.originalFetch === undefined) {{
            console.log('Patching fetch with API key');
            window.originalFetch = window.fetch;
            window.fetch = function(input, init = {{}}) {{
                init.headers = init.headers || {{}};
                init.headers['X-Api-Key'] = sessionStorage.getItem('apiKey');
                console.log('Patched fetch', input, init);
                return window.originalFetch(input, init);
            }};
        }}
        sessionStorage.setItem('authType', 'ApiKey');
        sessionStorage.setItem('apiKey', {});
    )", safe_api_key);

    return {
        {BrowserLogicCommandType::RunScript, script},
        {BrowserLogicCommandType::DoReload, std::string()},
        {BrowserLogicCommandType::ClearBasicAuth, std::string()}
    };
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::send_credentials()
{
    SPDLOG_DEBUG("{}", __FUNCTION__);
    m_api_key_sent = true;

    nlohmann::json payload = {{"username", m_usr}, {"password", m_psk}};
    return {
        {BrowserLogicCommandType::RunScript, "sessionStorage.removeItem('authType'); sessionStorage.removeItem('apiKey'); console.log('Session Storage cleared');"},
        {BrowserLogicCommandType::SetBasicAuth, payload.dump()}
    };
}

void BrowserLogicPhysicalPrinter::emplace_define_css_commands(std::vector<BrowserLogicCommand>& res)
{
    if (m_styles_defined) return;
    SPDLOG_DEBUG("{}", __FUNCTION__);
    m_styles_defined = true;

    std::string script;

#if defined(__APPLE__)
    script += R"(
        document.addEventListener('keydown', function (event) {
            if (event.key === 'F5' || (event.ctrlKey && event.key === 'r') || (event.metaKey && event.key === 'r')) {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'reloadHomePage', fromKeyboard: true}));
            }
            if (event.metaKey && event.key === 'q') {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'appQuit'}));
            }
            if (event.metaKey && event.key === 'm') {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'appMinimize'}));
            }
        });
    )";
#endif

    if (!script.empty()) {
        res.emplace_back(BrowserLogicCommandType::RunScript, script);
    }
}

std::vector<BrowserLogicCommand> BrowserLogicPhysicalPrinter::on_reload_event(const std::string& message_data)
{
    SPDLOG_DEBUG("{}", __FUNCTION__);
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        m_auto_retry_count = 0;

        bool from_keyboard = j.contains("fromKeyboard") && j["fromKeyboard"].is_boolean() && j["fromKeyboard"].get<bool>();
        if (from_keyboard) {
            return {{BrowserLogicCommandType::DoReload, {}}};
        }

        m_base_url_preloaded = false;
        return {{BrowserLogicCommandType::LoadURL, m_base_url}};
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Physical Printer reload message. {}", e.what());
        return {};
    }
}

std::string BrowserLogicPhysicalPrinter::script_show_loading_overlay() const
{
    return R"(
(function() {
    if (!document.getElementById('slic3r-custom-css')) {
        var style = document.createElement('style');
        style.id = 'slic3r-custom-css';
        style.innerHTML =
            '.slic3r-loading-overlay{position:fixed;top:0;left:0;right:0;bottom:0;' +
            'background-color:rgba(127,127,127,0.85);z-index:99999;' +
            'display:flex;align-items:center;justify-content:center;}' +
            '.slic3r-loading-anim{width:60px;aspect-ratio:4;' +
            '--_g:no-repeat radial-gradient(circle closest-side,#000 90%,#0000);' +
            'background:var(--_g) 0% 50%,var(--_g) 50% 50%,var(--_g) 100% 50%;' +
            'background-size:calc(100%/3) 100%;' +
            'animation:slic3r-loading-anim 1s infinite linear;}' +
            '@keyframes slic3r-loading-anim{' +
            '33%{background-size:calc(100%/3) 0%,calc(100%/3) 100%,calc(100%/3) 100%}' +
            '50%{background-size:calc(100%/3) 100%,calc(100%/3) 0%,calc(100%/3) 100%}' +
            '66%{background-size:calc(100%/3) 100%,calc(100%/3) 100%,calc(100%/3) 0%}}';
        document.head.appendChild(style);
    }
    if (!document.getElementById('slic3r-loading-overlay')) {
        var overlay = document.createElement('div');
        overlay.className = 'slic3r-loading-overlay';
        overlay.id = 'slic3r-loading-overlay';
        overlay.innerHTML = '<div class="slic3r-loading-anim"></div>';
        document.body.appendChild(overlay);
    }
})();
)";
}

std::string BrowserLogicPhysicalPrinter::script_navigate_to_path(const std::string& path) const
{
    std::string safe_path = nlohmann::json(path).dump();
    return fmt::format(R"(
(function() {{
    var path = {};
    var pathSlash = path.endsWith('/') ? path : path + '/';
    function tryNav(attempts) {{
        var link = document.querySelector('a[href="' + pathSlash + '"]') ||
                   document.querySelector('a[href="' + path + '"]');
        if (link) {{
            link.click();
        }} else if (attempts > 0) {{
            setTimeout(function() {{ tryNav(attempts - 1); }}, 50);
        }} else {{
            window.location.href = pathSlash;
        }}
    }}
    tryNav(100);
}})();
)", safe_path);
}

} // namespace Slic3r::App::Browser
