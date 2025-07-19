#ifndef slic3r_ConnectRequestHandler_hpp_
#define slic3r_ConnectRequestHandler_hpp_

#include <map>
#include <string>
#include  <functional>
#include <wx/string.h>
#include <wx/dialog.h>
#include <wx/window.h>

//#define DEBUG_URL_PANEL

namespace Slic3r::GUI {
class ConnectRequestHandler
{
public:
    ConnectRequestHandler();
    ~ConnectRequestHandler();

    void handle_message(const std::string& message);
    void resend_config();
protected:
    // action callbacks stored in m_actions
    virtual void on_connect_action_log(const std::string& message_data);
    virtual void on_connect_action_error(const std::string& message_data);
    virtual void on_connect_action_request_login(const std::string& message_data);
    virtual void on_connect_action_request_config(const std::string& message_data);
    virtual void on_connect_action_request_open_in_browser(const std::string& message_data);
    virtual void on_connect_action_select_printer(const std::string& message_data) = 0;
    virtual void on_connect_action_print(const std::string& message_data) = 0;
    virtual void on_connect_action_webapp_ready(const std::string& message_data) = 0;
    virtual void on_connect_action_close_dialog(const std::string& message_data) = 0;
    virtual void on_reload_event(const std::string& message_data) = 0;
    virtual void run_script_bridge(const wxString &script) = 0;

    std::map<std::string, std::function<void(const std::string&)>> m_actions;
};

class SourceViewDialog : public wxDialog
{
public:
    SourceViewDialog(wxWindow* parent, wxString source);
};

} // namespace Slic3r::GUI
#endif /* slic3r_ConnectRequestHandler_hpp_ */