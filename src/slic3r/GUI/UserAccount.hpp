#ifndef slic3r_UserAccount_hpp_
#define slic3r_UserAccount_hpp_

#include "UserAccountCommunication.hpp"
#include "libslic3r/AppConfig.hpp"

#include <string>
#include <memory>
#include <boost/filesystem.hpp>

namespace Slic3r{
namespace GUI{

enum class ConnectPrinterState {
    CONNECT_PRINTER_OFFLINE,
    CONNECT_PRINTER_PRINTING,
    CONNECT_PRINTER_PAUSED,//?
    CONNECT_PRINTER_STOPPED,//?
    CONNECT_PRINTER_IDLE,
    CONNECT_PRINTER_FINISHED,
    CONNECT_PRINTER_READY, //?
    CONNECT_PRINTER_ATTENTION,
    CONNECT_PRINTER_BUSY,
    CONNECT_PRINTER_ERROR,
    CONNECT_PRINTER_STATE_COUNT
};
// printer preset name and std::vector<size_t> is vector of ConnectPrinterState counters
typedef std::map<std::string, std::vector<size_t>> ConnectPrinterStateMap;
typedef std::map< std::string, std::string> ConnectUUIDToPresetName;
// Class UserAccount should handle every request for entities outside PrusaSlicer like PrusaAuth or PrusaConnect.
// Outside communication is implemented in class UserAccountCommunication that runs separate thread. Results come back in events to Plater.
// All incoming data shoud be stored in UserAccount.
class UserAccount {
public:
    UserAccount(wxEvtHandler* evt_handler, Slic3r::AppConfig* app_config, const std::string& instance_hash);
    ~UserAccount();

    bool is_logged() const;
    void do_login();
    void do_logout();
    wxString generate_login_redirect_url() { return m_communication->generate_login_redirect_url();  }
    wxString get_login_redirect_url(const std::string& service = std::string()) { return m_communication->get_login_redirect_url(service);  }
    
    void set_remember_session(bool remember);
    void toggle_remember_session();
    bool get_remember_session();
    void enqueue_connect_status_action();
    void enqueue_connect_printer_models_action();
    void enqueue_avatar_old_action();
    void enqueue_avatar_new_action(const std::string& url);
    void enqueue_printer_data_action(const std::string& uuid);
    void request_refresh();
    // Clears all data and connections, called on logout or EVT_UA_RESET
    void clear();

    // Functions called from UI where events emmited from UserAccountSession are binded
    // Returns bool if data were correctly proccessed
    bool on_login_code_recieved(const std::string& url_message);
    bool on_user_id_success(const std::string data, std::string& out_username, bool after_token_success);
    // Called on EVT_UA_FAIL, triggers test after several calls
    void on_communication_fail();
    void on_race_lost();
    bool on_connect_printers_success(const std::string& data, AppConfig* app_config, bool& out_printers_changed);
    bool on_connect_uiid_map_success(const std::string& data, AppConfig* app_config, bool& out_printers_changed);

    void on_activate_app(bool active) { m_communication->on_activate_app(active); }

    std::string get_username() const { return m_username; }
    std::string get_access_token();
    std::string get_shared_session_key();
    const ConnectPrinterStateMap& get_printer_state_map() const { return m_printer_map; }
    boost::filesystem::path get_avatar_path(bool logged) const;

    const std::map<std::string, ConnectPrinterState>& get_printer_state_table() const { return printer_state_table; }

    void        set_current_printer_uuid_from_connect(const std::string& uuid) { m_current_printer_uuid_from_connect = uuid; }
    std::string get_current_printer_uuid_from_connect(const std::string& selected_printer_id) const;

    void        set_current_printer_data(const std::string& data) { m_current_printer_data_json_from_connect = data; }

    void        set_refresh_time(int seconds) { m_communication->set_refresh_time(seconds); }

    void        on_store_read_request() { m_communication->on_store_read_request(); }
private:
    void set_username(const std::string& username, bool store);
   
    std::string m_instance_hash; // used in avatar path

    std::unique_ptr<Slic3r::GUI::UserAccountCommunication> m_communication;
    
    ConnectPrinterStateMap              m_printer_map;
    ConnectUUIDToPresetName             m_printer_uuid_map;
    std::map<std::string, std::string>  m_account_user_data;
    std::string                         m_username;
    size_t                              m_fail_counter { 0 };
    std::string                         m_avatar_extension;    

    std::string                         m_current_printer_uuid_from_connect;
    std::string                         m_current_printer_data_json_from_connect;

    const std::map<std::string, ConnectPrinterState> printer_state_table = {
        {"OFFLINE"  , ConnectPrinterState::CONNECT_PRINTER_OFFLINE},
        {"PRINTING" , ConnectPrinterState::CONNECT_PRINTER_PRINTING},
        {"PAUSED"   , ConnectPrinterState::CONNECT_PRINTER_PAUSED},
        {"STOPPED"  , ConnectPrinterState::CONNECT_PRINTER_STOPPED},
        {"IDLE"     , ConnectPrinterState::CONNECT_PRINTER_IDLE},
        {"FINISHED" , ConnectPrinterState::CONNECT_PRINTER_FINISHED},
        {"READY"    , ConnectPrinterState::CONNECT_PRINTER_READY},
        {"ATTENTION", ConnectPrinterState::CONNECT_PRINTER_ATTENTION},
        {"BUSY"     , ConnectPrinterState::CONNECT_PRINTER_BUSY},
        {"ERROR"    , ConnectPrinterState::CONNECT_PRINTER_ERROR},
    };
};
}} // namespace slic3r::GUI
#endif // slic3r_UserAccount_hpp_
