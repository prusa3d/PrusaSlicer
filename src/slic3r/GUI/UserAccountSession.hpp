#ifndef slic3r_UserAccountSession_hpp_
#define slic3r_UserAccountSession_hpp_

#include "Event.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/Utils/ServiceConfig.hpp"

#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <memory>

namespace Slic3r {
namespace GUI {

using OpenPrusaAuthEvent = Event<std::pair<wxString,wxString>>;
using UserAccountSuccessEvent = Event<std::string>;
using UserAccountFailEvent = Event<std::string>;
using UserAccountTimeEvent = Event<int>;
wxDECLARE_EVENT(EVT_OPEN_PRUSAAUTH, OpenPrusaAuthEvent);
wxDECLARE_EVENT(EVT_UA_LOGGEDOUT, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_ID_USER_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_PRUSACONNECT_STATUS_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_PRUSACONNECT_PRINTER_MODELS_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_AVATAR_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_PRUSACONNECT_PRINTER_DATA_SUCCESS, UserAccountSuccessEvent);
wxDECLARE_EVENT(EVT_UA_FAIL, UserAccountFailEvent); // Soft fail - clears only after some number of fails
wxDECLARE_EVENT(EVT_UA_RESET, UserAccountFailEvent); // Hard fail - clears all
wxDECLARE_EVENT(EVT_UA_PRUSACONNECT_PRINTER_DATA_FAIL, UserAccountFailEvent); // Failed to get data for printer to select, soft fail, action does not repeat
wxDECLARE_EVENT(EVT_UA_REFRESH_TIME, UserAccountTimeEvent);

typedef std::function<void(const std::string& body)> UserActionSuccessFn;
typedef std::function<void(const std::string& body)> UserActionFailFn;

// UserActions implements different operations via trigger() method. Stored in m_actions.
enum class UserAccountActionID {
    USER_ACCOUNT_ACTION_DUMMY,
    USER_ACCOUNT_ACTION_REFRESH_TOKEN,
    USER_ACCOUNT_ACTION_CODE_FOR_TOKEN,
    USER_ACCOUNT_ACTION_USER_ID,
    USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN,
    USER_ACCOUNT_ACTION_TEST_CONNECTION,
    USER_ACCOUNT_ACTION_CONNECT_STATUS, // status of all printers by UUID
    USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS, // status of all printers by UUID with printer_model. Should be called once to save printer models.
    USER_ACCOUNT_ACTION_AVATAR,
    USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID,
};
class UserAction
{
public:
    UserAction(const std::string name, const std::string url) : m_action_name(name), m_url(url){}
    virtual ~UserAction() = default;
    virtual void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const = 0;

protected:
    std::string m_action_name;
    std::string m_url;
};

class UserActionGetWithEvent : public UserAction
{
public:
    UserActionGetWithEvent(const std::string name, const std::string url, wxEventType succ_event_type, wxEventType fail_event_type)
        : m_succ_evt_type(succ_event_type)
        , m_fail_evt_type(fail_event_type)
        , UserAction(name, url)
    {}
    ~UserActionGetWithEvent() {}
    void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const override;
private:
    wxEventType   m_succ_evt_type;
    wxEventType   m_fail_evt_type;
};

class UserActionPost : public UserAction
{
public:
    UserActionPost(const std::string name, const std::string url) : UserAction(name, url) {}
    ~UserActionPost() {}
    void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const override;
};

class DummyUserAction : public UserAction
{
public:
    DummyUserAction() : UserAction("Dummy", {}) {}
    ~DummyUserAction() {}
    void perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const override { }
};

struct ActionQueueData
{
    UserAccountActionID     action_id;
    UserActionSuccessFn     success_callback;
    UserActionFailFn        fail_callback;
    std::string             input;
};

class UserAccountSession
{
public:
    UserAccountSession(wxEvtHandler* evt_handler, const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, bool polling_enabled)
        : p_evt_handler(evt_handler)
        , m_access_token(access_token)
        , m_refresh_token(refresh_token)
        , m_shared_session_key(shared_session_key)
        , m_polling_action(polling_enabled ? UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS : UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY)
       
    {
        auto& sc = Utils::ServiceConfig::instance();
        
        // do not forget to add delete to destructor
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY] = std::make_unique<DummyUserAction>();
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN] = std::make_unique<UserActionPost>("EXCHANGE_TOKENS", sc.account_token_url());
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CODE_FOR_TOKEN] = std::make_unique<UserActionPost>("EXCHANGE_TOKENS", sc.account_token_url());
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_USER_ID] = std::make_unique<UserActionGetWithEvent>("USER_ID", sc.account_me_url(), EVT_UA_ID_USER_SUCCESS, EVT_UA_RESET);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN] = std::make_unique<UserActionGetWithEvent>("TEST_ACCESS_TOKEN", sc.account_me_url(), EVT_UA_ID_USER_SUCCESS, EVT_UA_FAIL);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_CONNECTION] = std::make_unique<UserActionGetWithEvent>("TEST_CONNECTION", sc.account_me_url(), wxEVT_NULL, EVT_UA_RESET);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS] = std::make_unique<UserActionGetWithEvent>("CONNECT_STATUS", sc.connect_status_url(), EVT_UA_PRUSACONNECT_STATUS_SUCCESS, EVT_UA_FAIL);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS] = std::make_unique<UserActionGetWithEvent>("CONNECT_PRINTER_MODELS", sc.connect_printer_list_url(), EVT_UA_PRUSACONNECT_PRINTER_MODELS_SUCCESS, EVT_UA_FAIL);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR] = std::make_unique<UserActionGetWithEvent>("AVATAR", sc.media_url(), EVT_UA_AVATAR_SUCCESS, EVT_UA_FAIL);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID] = std::make_unique<UserActionGetWithEvent>("USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID", sc.connect_printers_url(), EVT_UA_PRUSACONNECT_PRINTER_DATA_SUCCESS, EVT_UA_FAIL);
    }
    ~UserAccountSession()
    {
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CODE_FOR_TOKEN].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_USER_ID].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_TEST_CONNECTION].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR].reset(nullptr);
        m_actions[UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID].reset(nullptr);
    }
    void clear() {
        {
            std::lock_guard<std::mutex> lock(m_credentials_mutex);
            m_access_token.clear();
            m_refresh_token.clear();
            m_shared_session_key.clear();
        }
        m_proccessing_enabled = false;
    }

    // Functions that automatically enable action queu processing
    void init_with_code(const std::string& code, const std::string& code_verifier);
    void enqueue_action(UserAccountActionID id, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input);
    // Special enques, that sets callbacks.
    void enqueue_test_with_refresh();
    void enqueue_refresh(const std::string& body);

    void process_action_queue();
    bool is_initialized() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return !m_access_token.empty() || !m_refresh_token.empty();
    }
    bool is_enqueued(UserAccountActionID action_id) const;
    std::string get_access_token() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_access_token;
    }
    std::string get_refresh_token() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_refresh_token;
    }
    std::string get_shared_session_key() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_shared_session_key;
    }
    long long get_next_token_timeout() const {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_next_token_timeout;
    }

    //void set_polling_enabled(bool enabled) {m_polling_action = enabled ? UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS : UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY; }
    void set_polling_action(UserAccountActionID action) { m_polling_action = action; }
private:

    void refresh_fail_callback(const std::string& body);
    void cancel_queue();
    void code_exchange_fail_callback(const std::string& body);
    void token_success_callback(const std::string& body);
    std::string client_id() const { return Utils::ServiceConfig::instance().account_client_id(); }

    // false prevents action queu to be processed - no communication is done
    // sets to true by init_with_code or enqueue_action call
    bool        m_proccessing_enabled {false}; 
    // action when woken up on idle - switches between USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS and USER_ACCOUNT_ACTION_CONNECT_STATUS
    // set to USER_ACCOUNT_ACTION_DUMMY to switch off polling
    UserAccountActionID m_polling_action;

    // Section of following vars is guarded by this mutex
    mutable std::mutex m_credentials_mutex;
    std::string m_access_token;
    std::string m_refresh_token;
    std::string m_shared_session_key;
    long long m_next_token_timeout;
    // End of section guarded by m_credentials_mutex

    std::queue<ActionQueueData>                                    m_action_queue;
    std::deque<ActionQueueData>                                    m_priority_action_queue;
    std::map<UserAccountActionID, std::unique_ptr<UserAction>>     m_actions;

    wxEvtHandler* p_evt_handler;
};

}
}
#endif