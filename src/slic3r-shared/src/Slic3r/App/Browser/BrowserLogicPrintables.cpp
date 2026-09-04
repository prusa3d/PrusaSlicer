#include "Slic3r/App/Browser/BrowserLogicPrintables.hpp"

#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/AppConfig.hpp>
#include <Slic3r/App/Theme.hpp>
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Browser/BrowserLogicPrintablesToConnect.hpp"
#include "Slic3r/App/Browser/BrowserLogicLogInRedirect.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/FileDownloader/FileDownloaderJobData.hpp"
#include "Slic3r/Assert.hpp"


#include <nlohmann/json.hpp>
#include <regex>

namespace Slic3r::App::Browser {

BrowserLogicPrintables::BrowserLogicPrintables(Biz::ProjectInteractor& project_interactor) :
    AbstractBrowserLogic(Biz::Network::ServiceConfig::instance().printables_url(), {"ExternalApp"}),
    m_project_interactor(project_interactor)
{
    m_events["accessTokenExpired"] = std::bind(&BrowserLogicPrintables::on_printables_event_access_token_expired, this, std::placeholders::_1);
    m_events["printGcode"] = std::bind(&BrowserLogicPrintables::on_printables_event_print_gcode, this, std::placeholders::_1);
    m_events["downloadFile"] = std::bind(&BrowserLogicPrintables::on_printables_event_download_file, this, std::placeholders::_1);
    m_events["sliceFile"] = std::bind(&BrowserLogicPrintables::on_printables_event_slice_file, this, std::placeholders::_1);
    m_events["sliceFiles"] = std::bind(&BrowserLogicPrintables::on_printables_event_slice_files, this, std::placeholders::_1);
    m_events["requiredLogin"] = std::bind(&BrowserLogicPrintables::on_printables_event_required_login, this, std::placeholders::_1);
    m_events["openExternalUrl"] = std::bind(&BrowserLogicPrintables::on_printables_event_open_url, this, std::placeholders::_1);
    m_events["ready"] = std::bind(&BrowserLogicPrintables::on_printables_event_dummy, this, std::placeholders::_1);
    m_events["reloadHomePage"] = std::bind(&BrowserLogicPrintables::on_webview_reload_event, this, std::placeholders::_1);
}

std::string BrowserLogicPrintables::access_token()
{
    return m_project_interactor.user_account_interactor().access_token();
}

std::vector<BrowserLogicCommand>
BrowserLogicPrintables::on_navigation_request_webview_event(const std::string& new_url, const std::string& current_url)
{
    //SPDLOG_INFO("{} {}", __FUNCTION__, new_url);
    if (new_url.find(m_url) == 0) {
        m_reached_default_url = true;
        if (new_url == current_url) {
            // we need to do this to redefine css when reload is hit
            m_styles_defined = false;
        }
    } else if (m_reached_default_url && new_url.find("http") == 0) {
        SPDLOG_INFO("{} does not start with default url. Vetoing.", new_url);
        return {{BrowserLogicCommandType::Veto, {}}};
    } else if (m_reached_default_url && new_url.find("/web/" + m_loading_html) != std::string::npos)
    {
        // Do not allow back button to loading screen
        return {{BrowserLogicCommandType::Veto, {}}};
    }

    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_show_webview_event(bool show)
{
    //SPDLOG_INFO("{} {}", __FUNCTION__, show);
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::SetLoadDefaultURLOnErrorTrue, std::string());
    // We do not care about token here. This should be called only if webview was visited before.
    // So it is receiving calls from user account to log in / log out.
    if (!m_next_show_url.empty()) {
        result.emplace_back(BrowserLogicCommandType::LoadURL, url_lang_theme(m_next_show_url));
        m_next_show_url.clear();
    } else {
        result.emplace_back(BrowserLogicCommandType::RunScript, "window.location.reload();");   
    }
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_loaded_webview_event(const std::string& url)
{
    //SPDLOG_INFO("{} {}", __FUNCTION__, url);
std::vector<BrowserLogicCommand> result;
    if (url.find("/web/" + m_loading_html) != std::string::npos && m_load_default_url) {
        m_load_default_url = false;
        emplace_load_default_url_commands(result);
        return result;
    }

    if (url.find(m_url) == 0) {
        emplace_define_css_commands(result);
    } else {
        m_styles_defined = false;
    }

    if (m_refreshing_token) {
        result.emplace_back(BrowserLogicCommandType::RunScript, script_hide_loading_overlay());
    }

    result.emplace_back(BrowserLogicCommandType::SetLoadDefaultURLOnErrorFalse, std::string());
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_load_default_url()
{
    std::vector<BrowserLogicCommand> res;
    emplace_load_default_url_commands(res);
    return res;
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_user_account_id_success(bool is_refresh, const std::string& current_url)
{
    if (m_load_default_url) {
        return {};
    }
    SPDLOG_INFO("{}", __FUNCTION__);
    std::vector<BrowserLogicCommand> result;
    if (!is_refresh) {
        // Do full reload of page with access token in header
        // This is only way to pass token to printables
        emplace_load_default_url_commands(result, current_url);
        return result;
    }

    m_refreshing_token = true;

    result.emplace_back(BrowserLogicCommandType::RunScript, script_show_loading_overlay());
    result.emplace_back(BrowserLogicCommandType::RunScript, "window.postMessage(JSON.stringify({ event: 'accessTokenWillChange' }))");

    m_project_interactor.user_account_interactor().request_printables_secret_token();   

    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_user_account_logged_out(const std::string& current_url)
{
    if (m_load_default_url) {
        return {};
    }
    return log_out(current_url);
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_user_account_will_refresh()
{
    if (m_load_default_url) {
        return {};
    }
    //SPDLOG_INFO("{}", __FUNCTION__);
    return {{BrowserLogicCommandType::RunScript, "window.postMessage(JSON.stringify({ event: 'accessTokenWillChange' }))"}};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_printables_secret_token(const std::string& body)
{    
    std::vector<BrowserLogicCommand> result;
    result.emplace_back(BrowserLogicCommandType::RunScript, script_hide_loading_overlay());
    m_refreshing_token = false; 

    std::string token;
    try {
        nlohmann::json j = nlohmann::json::parse(body);
        if (j.contains("secretToken") && j["secretToken"].is_string()) {
            token = j["secretToken"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        SPDLOG_ERROR("Failed to retrieve Printables secret token - Account features might not work.");
    }

    if (token.empty()) {
        SPDLOG_ERROR("Failed to retrieve Printables secret token - Account features might not work.");
        // We failed to retrieve the secret token. Printables will sooner or later run into error state.
        // It is likely this is happening for a reason (f.e. severed connection), its better to let things fail than repeat the token exchange.
        return result;
    }

    nlohmann::json payload = {
        {"event", "secretTokenChange"},
        {"secretToken", token}
    };
    result.emplace_back(BrowserLogicCommandType::RunScript, fmt::format("window.postMessage(JSON.stringify({}));", payload.dump()));
    // We just pass the token to Printables, rest should be done by their side.
    // We used to "window.location.reload();" - that is probably not correct as it could be faster than proccessing the secret token.
    return result;
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_script_message_webview_event(const std::string& message)
{
    std::string event_string;
    try {
        nlohmann::json j = nlohmann::json::parse(message);
        if (j.contains("event") && j["event"].is_string()) {
            event_string = j["event"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        return {};
    }

    if (event_string.empty()) {
        SPDLOG_ERROR("Received invalid message from Printables (missing event). Message: {}", message);
        return {};
    }

    //SPDLOG_INFO("Printables Request: {}", event_string);
    if (m_events.find(event_string) == m_events.end())
    {
        SPDLOG_WARN("There is Printables Request that has no handling function. Event: {}", event_string);
        return {};
    }
    return m_events[event_string](message);
}

std::vector<BrowserLogicCommand>
BrowserLogicPrintables::on_printables_event_access_token_expired(const std::string& message_data)
{
    // { "event": "accessTokenExpired")
    // There seems to be a situation where we get accessTokenExpired when there is active token from Slicer POW
    // We need get new token and freeze webview until its not refreshed
    if (m_refreshing_token) {
        return {};
    }
    m_refreshing_token = true;
    m_project_interactor.user_account_interactor().request_refresh();
    return {{BrowserLogicCommandType::RunScript, script_show_loading_overlay()}};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_printables_event_print_gcode(const std::string& message_data)
{
    // { "event": "downloadFile", "url": "https://media.printables.com/somesecure.stl", "modelUrl": "https://www.printables.com/model/123" }
    std::string download_url;
    std::string model_url;
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("url") && j["url"].is_string()) {
            download_url = j["url"].get<std::string>();
        }
        if (j.contains("modelUrl") && j["modelUrl"].is_string()) {
            model_url = j["modelUrl"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        return {};
    }
    DEBUG_ASSERT(!download_url.empty() && !model_url.empty(), "Faulty printables message.");

    std::string final_url = Biz::Network::ServiceConfig::instance().connect_printables_print_url() + "?url=" + Biz::Network::IHttp::escape_string(download_url);
    // TODO use final_url

    AppServices::instance().dialog_manager().show_webview_dialog(std::make_unique<Browser::BrowserLogicPrintablesToConnect>(final_url, m_project_interactor), &m_project_interactor);

    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_printables_event_download_file(const std::string& message_data)
{
    // { "event": "downloadFile", "url": "https://media.printables.com/somesecure.stl", "modelUrl": "https://www.printables.com/model/123" }
    std::string download_url;
    std::string model_url;
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("url") && j["url"].is_string()) {
            download_url = j["url"].get<std::string>();
        }
        if (j.contains("modelUrl") && j["modelUrl"].is_string()) {
            model_url = j["modelUrl"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        return {};
    }
    DEBUG_ASSERT(!download_url.empty() && !model_url.empty(), "Faulty printables message.");

    Biz::FileDownloader::FileDownloaderJobInput info;
    info.file_url = download_url;
    info.project_url = model_url;
    info.load_count = 0;
    Biz::FileDownloader::FileDownloaderMultiTicket sources;
    sources.jobs.push_back(std::move(info));
    m_project_interactor.download_model_from_printables_tab(std::move(sources));
    return {{BrowserLogicCommandType::SwitchToSlicing, {}}};;
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_printables_event_slice_file(const std::string& message_data)
{
    // { "event": "downloadFile", "url": "https://media.printables.com/somesecure.stl", "modelUrl": "https://www.printables.com/model/123" }
    std::string download_url;
    std::string model_url;
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("url") && j["url"].is_string()) {
            download_url = j["url"].get<std::string>();
        }
        if (j.contains("modelUrl") && j["modelUrl"].is_string()) {
            model_url = j["modelUrl"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        return {};
    }
    DEBUG_ASSERT(!download_url.empty() && !model_url.empty(), "Faulty printables message.");

    Biz::FileDownloader::FileDownloaderJobInput info;
    info.file_url = download_url;
    info.project_url = model_url;
    info.load_count = 1;
    Biz::FileDownloader::FileDownloaderMultiTicket sources;
    sources.jobs.push_back(std::move(info));
    m_project_interactor.download_model_from_printables_tab(std::move(sources));
    return {{BrowserLogicCommandType::SwitchToSlicing, {}}};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_printables_event_slice_files(const std::string& message_data)
{

    Biz::FileDownloader::FileDownloaderMultiTicket sources;
     
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("new_project") && j["new_project"].is_boolean()) {
            sources.new_project = j["new_project"].get<bool>();
        }
        if (j.contains("objects") && j["objects"].is_array()) {
            for (const auto& obj : j["objects"]) {
                Biz::FileDownloader::FileDownloaderJobInput info;
                if (obj.contains("name") && obj["name"].is_string()) {
                    info.file_name = obj["name"].get<std::string>();
                }
                if (obj.contains("count") && obj["count"].is_number_integer()) {
                    info.load_count = obj["count"].get<int>();
                }
                if (obj.contains("source")) {
                    const auto& source = obj["source"];
                    if (source.contains("fileUrl") && source["fileUrl"].is_string()) {
                        info.file_url = source["fileUrl"].get<std::string>();
                    }
                    if (source.contains("name") && source["name"].is_string()) {
                        info.project_name = source["name"].get<std::string>();
                    }
                    if (source.contains("url") && source["url"].is_string()) {
                        info.project_url = source["url"].get<std::string>();
                    }
                    if (source.contains("imageUrl") && source["imageUrl"].is_string()) {
                        info.image_url = source["imageUrl"].get<std::string>();
                    }
                }
                sources.jobs.emplace_back(std::move(info));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        return {};
    }

    DEBUG_ASSERT(!sources.jobs.empty(), "Faulty printables message.");
    m_project_interactor.download_model_from_printables_tab(std::move(sources));        
   
    return {{BrowserLogicCommandType::SwitchToSlicing, {}}};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_printables_event_required_login(const std::string& message_data)
{
    if (!m_project_interactor.user_account_interactor().is_logged_in()) {
        AppServices::instance().dialog_manager().show_webview_dialog(
            std::make_unique<Browser::BrowserLogicLogInRedirect>(
                m_project_interactor.user_account_interactor()
            ),
            &m_project_interactor
        );
        if (m_project_interactor.raise_app_fn()) {
            m_project_interactor.raise_app_fn()();
        }
    }
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_printables_event_open_url(const std::string& message_data)
{
    std::string url;
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("url") && j["url"].is_string()) {
            url = j["url"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        return {};
    }
    return {{BrowserLogicCommandType::OpenExternalBrowser, url}};
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::on_webview_reload_event(const std::string& message_data)
{
    // Event from our error page button or keyboard shortcut
    m_styles_defined = false;
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("fromKeyboard") && j["fromKeyboard"].is_boolean() && j["fromKeyboard"].get<bool>())
        {
            return {{BrowserLogicCommandType::DoReload, {}}};
        } else {
            std::vector<BrowserLogicCommand> res;
            emplace_load_default_url_commands(res);
            return res;
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse Printables message. {}", e.what());
        return {};
    }
}

std::vector<BrowserLogicCommand> BrowserLogicPrintables::log_out(const std::string& url)
{
    std::vector<BrowserLogicCommand> result;
    m_refreshing_token = false;
    m_styles_defined   = false;
    result.emplace_back(BrowserLogicCommandType::RunScript, script_hide_loading_overlay());
    result.emplace_back(
        BrowserLogicCommandType::DeleteCookies,
        Biz::Network::ServiceConfig::instance().printables_url()
    );
    result.emplace_back(BrowserLogicCommandType::RunScript, "localStorage.clear();");

    std::string next_url = url_lang_theme(url);

    result.emplace_back(BrowserLogicCommandType::LoadURL, next_url);
    return result;
}

std::string BrowserLogicPrintables::script_hide_loading_overlay() const
{
    return R"(
        function slic3r_hideLoadingOverlay() {
            const overlayDiv = document.getElementById('slic3r-loading-overlay');
            if (overlayDiv)
                overlayDiv.remove();
        }
        slic3r_hideLoadingOverlay();
    )";
}

std::string BrowserLogicPrintables::script_show_loading_overlay() const
{
    return R"(
        function slic3r_showLoadingOverlay() {
            if (document.getElementById('slic3r-loading-overlay')) return;
            
            const overlayDiv = document.createElement('div');
            overlayDiv.className = 'slic3r-loading-overlay';
            overlayDiv.id = 'slic3r-loading-overlay';
            overlayDiv.innerHTML = '<div class="slic3r-loading-anim"></div>';
            document.body.appendChild(overlayDiv);
        }
        slic3r_showLoadingOverlay();
    )";
}

void BrowserLogicPrintables::emplace_define_css_commands(std::vector<BrowserLogicCommand>& res)
{
    if (m_styles_defined) {
        return;
    }
    m_styles_defined = true;

    std::string script = R"(
        // Loading overlay and Notification style
        if (!document.getElementById('slic3r-custom-css')) {
            var style = document.createElement('style');
            style.id = 'slic3r-custom-css';
            style.innerHTML = `
            body {}
            .slic3r-loading-overlay {
                position: fixed;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background-color: rgba(127 127 127 / 50%);
                z-index: 50;
                display: flex;
                align-items: center;
                justify-content: center;
            }
            .slic3r-loading-anim {
                width: 60px;
                aspect-ratio: 4;
                --_g: no-repeat radial-gradient(circle closest-side,#000 90%,#0000);
                background:
                        var(--_g) 0%   50%,
                        var(--_g) 50%  50%,
                        var(--_g) 100% 50%;
                background-size: calc(100%/3) 100%;
                animation: slic3r-loading-anim 1s infinite linear;
            }
            @keyframes slic3r-loading-anim {
                33%{background-size:calc(100%/3) 0%  ,calc(100%/3) 100%,calc(100%/3) 100%}
                50%{background-size:calc(100%/3) 100%,calc(100%/3) 0%  ,calc(100%/3) 100%}
                66%{background-size:calc(100%/3) 100%,calc(100%/3) 100%,calc(100%/3) 0%  }
            }
            .notification-popup {
                position: fixed;
                right: 10px;
                bottom: 10px;
                background-color: #333333; /* Dark background */
                padding: 10px;
                border-radius: 6px; /* Slightly rounded corners */
                color: #ffffff; /* White text */
                font-family: Arial, sans-serif;
                font-size: 12px;
                display: flex;
                justify-content: space-between;
                align-items: center;
                box-shadow: 0px 4px 8px rgba(0, 0, 0, 0.3); /* Add a subtle shadow */
                min-width: 350px; 
                max-width: 350px;
                min-height: 50px;
            }
            .notification-popup div {
                white-space: nowrap;
                overflow: hidden;
                text-overflow: ellipsis;
                padding-right: 20px; /* Add padding to make text truncate earlier */
            }
            .notification-popup b {
                color: #ffa500;
            }
            .notification-popup a:hover {
                text-decoration: underline; /* Underline on hover */
            }
            .notification-popup .close-button {
                display: inline-block;
                width: 20px;
                height: 20px;
                border: 2px solid #ffa500; /* Orange border for the button */
                border-radius: 4px;
                text-align: center;
                font-size: 16px;
                line-height: 16px;
                cursor: pointer;
                padding-top: 1px; 
            }
            .notification-popup .close-button:hover {
                background-color: #ffa500; /* Orange background on hover */
                color: #333333; /* Dark color for the "X" on hover */
            }
            .notification-popup .close-button:before {
                content: 'X';
                color: #ffa500; /* Orange "X" */
                font-weight: bold;
            }
            `;
            document.head.appendChild(style); 
        }
        (function() {
            const listenerKey = 'custom-click-listener';
            if (!document[listenerKey]) {
                document.addEventListener('click', function(event) {
                    const target = event.target.closest('a[href]');
                    if (!target) return; 

                    const url = target.href;
                    if (url === 'about:blank') return; 

                    console.log(`Printables:onNavigationRequest: ${url}`);

                    // Updated Regex to include testprusaverse.com
                    if (!/printables\.com|prusaverse\.com/.test(url)) {
                        window.ExternalApp.postMessage(JSON.stringify({ event: 'openExternalUrl', url }))
                        event.preventDefault();
                    }
                }, true);
                document[listenerKey] = true;
            }
        })();
    )";
#if defined(__APPLE__)
    // WebView on Windows does read keyboard shortcuts
    // Thus doing f.e. Reload twice would make the oparation to fail
    script += R"(
        document.addEventListener('keydown', function (event) {
            if (event.key === 'F5' || (event.ctrlKey && event.key === 'r') || (event.metaKey && event.key === 'r')) {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'reloadHomePage', fromKeyboard: 1}));
            }
            if (event.metaKey && event.key === 'q') {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'appQuit'}));
            }
            if (event.metaKey && event.key === 'm') {
                window.ExternalApp.postMessage(JSON.stringify({ event: 'appMinimize'}));
            }
        });
    )";
#endif // defined(__APPLE__)
    res.emplace_back(BrowserLogicCommandType::RunScript, script);
}

std::string BrowserLogicPrintables::url_lang_theme(const std::string& url) const
{
    // situations and reaction:
    // 1) url is just a path (no query no fragment) -> query with lang and theme is added
    // 2) url has query that contains lang and theme -> query and lang values are modified
    // 3) url has query with just one of lang or theme -> query is modified and missing value is added
    // 4) url has query of query and fragment without lang and theme -> query with lang and theme is added to the end of query

    

    std::string url_string = url;
    std::string theme =
        AppServices::instance().app_config().get<Theme::Style>("theme") == Theme::Style::Light ?
        "light" :
        "dark";
    std::string lang_app_conf =
        AppServices::instance().app_config().get<std::string>("translation_language");
    if (lang_app_conf.size() > 2)
        lang_app_conf = lang_app_conf.substr(0, 2);

    // Replace lang and theme if already in url
    bool lang_found = false;
    std::regex lang_regex(R"((lang=)[^&#]*)");
    if (std::regex_search(url_string, lang_regex)) {
        url_string = std::regex_replace(url_string, lang_regex, "$1" + lang_app_conf);
        lang_found = true;
    }
    bool theme_found = false;
    std::regex theme_regex(R"((theme=)[^&#]*)");
    if (std::regex_search(url_string, theme_regex)) {
        url_string  = std::regex_replace(url_string, theme_regex, "$1" + theme);
        theme_found = true;
    }
    if (lang_found && theme_found)
        return url_string;

    // missing params string
    std::string new_params = lang_found ?
        "theme=" + theme :
        theme_found ?
        "lang=" + lang_app_conf :
        "lang=" + lang_app_conf + "&theme=" + theme;

    // Regex to capture query and optional fragment
    std::regex query_regex(R"((\?.*?)(#.*)?$)");

    if (std::regex_search(url_string, query_regex)) {
        // Append params before the fragment (if it exists)
        return std::regex_replace(url_string, query_regex, "$1&" + new_params + "$2");
    }
    std::regex fragment_regex(R"(#.*$)");
    if (std::regex_search(url_string, fragment_regex)) {
        // Add params before the fragment
        return std::regex_replace(url_string, fragment_regex, "?" + new_params + "$&");
    }

    return url_string + "?" + new_params;
}

void BrowserLogicPrintables::emplace_load_default_url_commands(std::vector<BrowserLogicCommand>& res, const std::string& override_url)
{
    
    res.emplace_back(BrowserLogicCommandType::RunScript, script_hide_loading_overlay());
    m_styles_defined = false;
    std::string url_base = override_url.empty() ? Biz::Network::ServiceConfig::instance().printables_url()  + "/homepage" : override_url;
    std::string final_url = url_lang_theme(url_base);
    const std::string access_token = m_project_interactor.user_account_interactor().access_token();
    // in case of opening printables logged out - delete cookies and localstorage to get rid of last login
    //SPDLOG_INFO("{} {} {}", __FUNCTION__, override_url, !access_token.empty());
    if (access_token.empty()) {
        res.emplace_back(
            BrowserLogicCommandType::DeleteCookies,
            Biz::Network::ServiceConfig::instance().printables_url()
        );
        res.emplace_back(BrowserLogicCommandType::AddUserScript, "localStorage.clear();");
        res.emplace_back(BrowserLogicCommandType::LoadURL, std::move(final_url));
        return;
    }

    res.emplace_back(BrowserLogicCommandType::RunScript, "window.postMessage(JSON.stringify({ event: 'accessTokenWillChange' }))");
    res.emplace_back(BrowserLogicCommandType::LoadRequest, std::move(final_url));
}

} // namespace Slic3r::App::Browser
