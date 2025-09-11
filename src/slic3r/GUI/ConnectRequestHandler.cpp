#include "ConnectRequestHandler.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/UserAccount.hpp"

#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

namespace Slic3r::GUI {

ConnectRequestHandler::ConnectRequestHandler()
{
    m_actions["REQUEST_LOGIN"] = std::bind(&ConnectRequestHandler::on_connect_action_request_login, this, std::placeholders::_1);
    m_actions["REQUEST_CONFIG"] = std::bind(&ConnectRequestHandler::on_connect_action_request_config, this, std::placeholders::_1);
    m_actions["WEBAPP_READY"] = std::bind(&ConnectRequestHandler::on_connect_action_webapp_ready,this, std::placeholders::_1);
    m_actions["SELECT_PRINTER"] = std::bind(&ConnectRequestHandler::on_connect_action_select_printer, this, std::placeholders::_1);
    m_actions["PRINT"] = std::bind(&ConnectRequestHandler::on_connect_action_print, this, std::placeholders::_1);
    m_actions["REQUEST_OPEN_IN_BROWSER"] = std::bind(&ConnectRequestHandler::on_connect_action_request_open_in_browser, this, std::placeholders::_1);
    m_actions["ERROR"] = std::bind(&ConnectRequestHandler::on_connect_action_error, this, std::placeholders::_1);
    m_actions["LOG"] = std::bind(&ConnectRequestHandler::on_connect_action_log, this, std::placeholders::_1);
    m_actions["RELOAD_HOME_PAGE"] = std::bind(&ConnectRequestHandler::on_reload_event, this, std::placeholders::_1);
    m_actions["CLOSE_DIALOG"] = std::bind(&ConnectRequestHandler::on_connect_action_close_dialog, this, std::placeholders::_1);
}
ConnectRequestHandler::~ConnectRequestHandler()
{
}
void ConnectRequestHandler::handle_message(const std::string& message)
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
        std::stringstream ss(message);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        // v1:
        if (const auto action = ptree.get_optional<std::string>("action"); action) {
            action_string = *action;
        }
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse _prusaConnect message. " << e.what();
        return;
    }

    if (action_string.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Received invalid message from _prusaConnect (missing action). Message: " << message;
        return;
    }
    assert(m_actions.find(action_string) != m_actions.end()); // this assert means there is a action that has no handling.
    if (m_actions.find(action_string) != m_actions.end()) {
        m_actions[action_string](message);
    }
}

void ConnectRequestHandler::on_connect_action_error(const std::string &message_data)
{
    BOOST_LOG_TRIVIAL(error) << "WebView runtime error: " << message_data;
}

void ConnectRequestHandler::resend_config()
{
    on_connect_action_request_config({});
}

void ConnectRequestHandler::on_connect_action_log(const std::string& message_data)
{
    BOOST_LOG_TRIVIAL(info) << "WebView log: " << message_data;
}

void ConnectRequestHandler::on_connect_action_request_login(const std::string &message_data)
{}

void ConnectRequestHandler::on_connect_action_request_config(const std::string& message_data)
{
    /*
    accessToken?: string;
    clientVersion?: string;
    colorMode?: "LIGHT" | "DARK";
    language?: ConnectLanguage;
    sessionId?: string;
    */
    const std::string token = wxGetApp().plater()->get_user_account()->get_access_token();
    //const std::string sesh = wxGetApp().plater()->get_user_account()->get_shared_session_key();
    const std::string dark_mode = wxGetApp().dark_mode() ? "DARK" : "LIGHT";
    wxString language = GUI::wxGetApp().current_language_code();
    language = language.SubString(0, 1);
    const std::string init_options = GUI::format("{\"accessToken\": \"%4%\",\"clientVersion\": \"%1%\", \"colorMode\": \"%2%\", \"language\": \"%3%\"}", SLIC3R_VERSION, dark_mode, language, token );  
    wxString script = GUI::format_wxstr("window._prusaConnect_v2.init(%1%)", init_options);
    run_script_bridge(script);
    
}
void ConnectRequestHandler::on_connect_action_request_open_in_browser(const std::string& message_data) 
{
    try {
        std::stringstream ss(message_data);
        pt::ptree ptree;
        pt::read_json(ss, ptree);
        if (const auto url = ptree.get_optional<std::string>("url"); url) {
            wxGetApp().open_browser_with_warning_dialog(GUI::from_u8(*url));
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Could not parse _prusaConnect message. " << e.what();
        return;
    }    
}

SourceViewDialog::SourceViewDialog(wxWindow* parent, wxString source) :
                  wxDialog(parent, wxID_ANY, "Source Code",
                           wxDefaultPosition, wxSize(700,500),
                           wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, source,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE |
                                      wxTE_RICH |
                                      wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
}
} // namespace Slic3r::GUI