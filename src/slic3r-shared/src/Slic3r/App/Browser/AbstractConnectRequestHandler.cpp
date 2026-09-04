#include "Slic3r/App/Browser/AbstractConnectRequestHandler.hpp"

#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/AppConfig.hpp>
#include <Slic3r/App/Theme.hpp>
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/LegacyFormat.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r::App::Browser {
AbstractConnectRequestHandler::AbstractConnectRequestHandler(Biz::ProjectInteractor& project_interactor)
    : m_project_interactor(project_interactor)
{
    m_actions["REQUEST_LOGIN"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_request_login, this, std::placeholders::_1);
    m_actions["REQUEST_CONFIG"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_request_config, this, std::placeholders::_1);
    m_actions["WEBAPP_READY"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_webapp_ready,this, std::placeholders::_1);
    m_actions["SELECT_PRINTER"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_select_printer, this, std::placeholders::_1);
    m_actions["PRINT"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_print, this, std::placeholders::_1);
    m_actions["REQUEST_OPEN_IN_BROWSER"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_request_open_in_browser, this, std::placeholders::_1);
    m_actions["ERROR"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_error, this, std::placeholders::_1);
    m_actions["LOG"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_log, this, std::placeholders::_1);
    m_actions["RELOAD_HOME_PAGE"] = std::bind(&AbstractConnectRequestHandler::on_webview_reload_event, this, std::placeholders::_1);
    m_actions["CLOSE_DIALOG"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_close_dialog, this, std::placeholders::_1);
    m_actions["LOG_IN_IN_BROWSER"] = std::bind(&AbstractConnectRequestHandler::on_connect_action_log_in_in_browser, this, std::placeholders::_1);
}

std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::handle_message(const std::string& message)
{
    // read msg and choose action
    /*
    v0:
    {"type":"request","detail":{"action":"requestAccessToken"}}
    v1:
    {"action":"REQUEST_ACCESS_TOKEN"}
    */
    std::string action_string;
    try {
        nlohmann::json j = nlohmann::json::parse(message);
        if (j.contains("action") && j["action"].is_string()) {
            action_string = j["action"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse _prusaConnect message. {}", e.what());
        return {};
    }

    if (action_string.empty()) {
        SPDLOG_ERROR("Received invalid message from _prusaConnect (missing action). Message: {}", message);
        return {};
    }

    SPDLOG_INFO("Connect Request: {}", action_string);
    if (m_actions.find(action_string) == m_actions.end())
    {
        SPDLOG_WARN("There is Connect Request that has no handling function. Action: {}", action_string);
        return {};
    }
    return m_actions[action_string](message);
}

std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::on_connect_action_error(const std::string &message_data)
{
    SPDLOG_ERROR("WebView runtime error: {}", message_data);
    return {};
}

std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::resend_config()
{
    return on_connect_action_request_config({});
}

std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::on_connect_action_log(const std::string& message_data)
{
    SPDLOG_ERROR("WebView log: {}", message_data);
    return {};
}

std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::on_connect_action_request_login(const std::string &message_data)
{
    m_project_interactor.user_account_interactor().validate_and_refresh();
    return {};
}

std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::on_connect_action_log_in_in_browser(const std::string& message_data)
{
    return {};
}

std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::on_connect_action_request_config(const std::string& message_data)
{
    /*
    accessToken?: string;
    clientVersion?: string;
    colorMode?: "LIGHT" | "DARK";
    language?: ConnectLanguage;
    sessionId?: string;
    */
    
    if(m_project_interactor.user_account_interactor().validate_and_refresh()) {
        // Here account just realised access token is not valid and it will be refreshed.
        // We cannot send the config here as we do not have valid token. 
        // Lets keep silence for now. So we do not spam more REQUEST_CONFIG due to expired token.
        // TODO: Gray overlay would be nice.
        return {};
    }

    const std::string theme =
        AppServices::instance().app_config().get<Theme::Style>("theme") == Theme::Style::Light ?
        "LIGHT" :
        "DARK";
    std::string lang_app_conf =
        AppServices::instance().app_config().get<std::string>("translation_language");

    const std::string init_options = format(
        "{\"accessToken\": \"%4%\",\"clientVersion\": \"%1%\", \"colorMode\": \"%2%\", \"language\": \"%3%\"}",
        /*SLIC3R_VERSION*/ "2.9.2",
        theme,
        lang_app_conf,
        m_project_interactor.user_account_interactor().access_token()
    );
    std::string script = format("window._prusaConnect_v2.init(%1%)", init_options);
    return {{BrowserLogicCommandType::RunScript, script}};
}
std::vector<BrowserLogicCommand> AbstractConnectRequestHandler::on_connect_action_request_open_in_browser(const std::string& message_data) 
{
    try {
        nlohmann::json j = nlohmann::json::parse(message_data);
        if (j.contains("url") && j["url"].is_string()) {
            // TODO: open browser with url
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse _prusaConnect message. {}", e.what());
        return {};
    }
    return {};
}

} // namespace Slic3r::App::Browser
