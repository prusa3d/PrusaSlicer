#include "UserAccountSession.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "../Utils/Http.hpp"
#include "../Utils/Jwt.hpp"
#include "I18N.hpp"

#include <boost/log/trivial.hpp>
#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <curl/curl.h>
#include <string>

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_OPEN_PRUSAAUTH, OpenPrusaAuthEvent);
wxDEFINE_EVENT(EVT_UA_LOGGEDOUT, UserAccountSuccessEvent);
wxDEFINE_EVENT(EVT_UA_ID_USER_SUCCESS, UserAccountSuccessEvent);
wxDEFINE_EVENT(EVT_UA_ID_USER_SUCCESS_AFTER_TOKEN_SUCCESS, UserAccountSuccessEvent);
wxDEFINE_EVENT(EVT_UA_SUCCESS, UserAccountSuccessEvent);
wxDEFINE_EVENT(EVT_UA_PRUSACONNECT_STATUS_SUCCESS, UserAccountSuccessEvent);
wxDEFINE_EVENT(EVT_UA_PRUSACONNECT_PRINTER_MODELS_SUCCESS, UserAccountSuccessEvent);
wxDEFINE_EVENT(EVT_UA_AVATAR_SUCCESS, UserAccountSuccessEvent); 
wxDEFINE_EVENT(EVT_UA_PRUSACONNECT_PRINTER_DATA_SUCCESS, UserAccountSuccessEvent);
wxDEFINE_EVENT(EVT_UA_FAIL, UserAccountFailEvent);
wxDEFINE_EVENT(EVT_UA_RESET, UserAccountFailEvent);
wxDEFINE_EVENT(EVT_UA_RACE_LOST, UserAccountFailEvent);
wxDEFINE_EVENT(EVT_UA_PRUSACONNECT_PRINTER_DATA_FAIL, UserAccountFailEvent);
wxDEFINE_EVENT(EVT_UA_REFRESH_TIME, UserAccountTimeEvent);
wxDEFINE_EVENT(EVT_UA_ENQUEUED_REFRESH, SimpleEvent);
wxDEFINE_EVENT(EVT_UA_RETRY_NOTIFY, UserAccountFailEvent);
wxDEFINE_EVENT(EVT_UA_CLOSE_RETRY_NOTIFICATION, SimpleEvent);

void UserActionPost::perform(/*UNUSED*/ wxEvtHandler* evt_handler, /*UNUSED*/ const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const
{
    std::string url = m_url;
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " " << url; 
    auto http = Http::post(std::move(url));
    if (!input.empty())
        http.set_post_body(input);
    http.header("Content-type", "application/x-www-form-urlencoded");
    http.on_error([fail_callback](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(trace) << "UserActionPost::perform on_error";
        if (fail_callback)
            fail_callback(body);
    });
    http.on_complete([success_callback](std::string body, unsigned status) {
        BOOST_LOG_TRIVIAL(trace) << "UserActionPost::perform on_complete";
        if (success_callback)
            success_callback(body);
    });
    http.on_retry([&](int attempt, unsigned delay) {
        BOOST_LOG_TRIVIAL(trace) << "UserActionPost::perform on_retry " << attempt;
        if (attempt > 1) {
             wxQueueEvent(evt_handler, new UserAccountFailEvent(EVT_UA_RETRY_NOTIFY, GUI::format(_u8L("Communication with Prusa Account is taking longer than expected. Retrying. Attempt %1%."), std::to_string(attempt))));
        }
        return true;
    });
    http.perform_sync(HttpRetryOpt::default_retry());
}

void UserActionGetWithEvent::perform(wxEvtHandler* evt_handler, const std::string& access_token, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input) const
{
    std::string url = m_url + input;
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " " << url; 
    auto http = Http::get(std::move(url));
    if (!access_token.empty()) {
        http.header("Authorization", "Bearer " + access_token);
#ifndef _NDEBUG
        // In debug mode, also verify the token expiration
        // This is here to help with "dev" accounts with shorten (sort of faked) expiration time
        // The /api/v1/me will accept these tokens even if these are fake-marked as expired
        if (!Utils::verify_exp(access_token) && fail_callback) {
            fail_callback("Token Expired");
        }
#endif
    }
    http.on_error([evt_handler, fail_callback, action_name = &m_action_name, fail_evt_type = m_fail_evt_type](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(trace) << "UserActionGetWithEvent::perform on_error";
        if (fail_callback)
            fail_callback(body);
        std::string message = GUI::format("%1% action failed (%2%): %3%", action_name, std::to_string(status), body);
        if (fail_evt_type != wxEVT_NULL)
            wxQueueEvent(evt_handler, new UserAccountFailEvent(fail_evt_type, std::move(message)));
    });
    http.on_complete([evt_handler, success_callback, succ_evt_type = m_succ_evt_type](std::string body, unsigned status) {
        BOOST_LOG_TRIVIAL(trace) << "UserActionGetWithEvent::perform on_complete";
        if (success_callback)
            success_callback(body);
        if (succ_evt_type != wxEVT_NULL)
            wxQueueEvent(evt_handler, new UserAccountSuccessEvent(succ_evt_type, body));
    });
    http.on_retry([&](int attempt, unsigned delay) {
        BOOST_LOG_TRIVIAL(trace) << "UserActionGetWithEvent::perform on_retry " << attempt;
        if (attempt > 1) {
             wxQueueEvent(evt_handler, new UserAccountFailEvent(EVT_UA_RETRY_NOTIFY, GUI::format(_u8L("Communication with Prusa Account is taking longer than expected. Retrying. Attempt %1%."), std::to_string(attempt))));
        }
        return true;
    });
    http.perform_sync(HttpRetryOpt::default_retry());
}

bool UserAccountSession::is_enqueued(UserAccountActionID action_id) const 
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        return std::any_of(
            std::begin(m_priority_action_queue), std::end(m_priority_action_queue),
            [action_id](const ActionQueueData& item) { return item.action_id == action_id; }
        );
    }
}

void UserAccountSession::process_action_queue()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_proccessing_enabled)
            return;
        BOOST_LOG_TRIVIAL(trace) << "action queue: " << m_priority_action_queue.size() << " " << m_action_queue.size();
        if (m_priority_action_queue.empty() && m_action_queue.empty()) {
            // update printers periodically
            enqueue_action_inner(m_polling_action, nullptr, nullptr, {});
        }
    }
    process_action_queue_inner();
}
void UserAccountSession::process_action_queue_inner()
{
    bool call_priority = false;
    bool call_standard = false;
    ActionQueueData selected_data;
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
    
        // priority queue works even when tokens are empty or broken
        if (!m_priority_action_queue.empty()) {
            // Do a copy here even its costly. We need to get data outside m_session_mutex protected code to perform background operation over it.
            selected_data = m_priority_action_queue.front();
            m_priority_action_queue.pop_front();
            call_priority = true;
        } else if (this->is_initialized() && !m_action_queue.empty()) {
            // regular queue has to wait until priority fills tokens
            // Do a copy here even its costly. We need to get data outside m_session_mutex protected code to perform background operation over it.
            selected_data = m_action_queue.front();
            m_action_queue.pop();
            call_standard = true;
        }
    }
    if (call_priority || call_standard) {
        bool use_token = m_actions[selected_data.action_id]->get_requires_auth_token();
        m_actions[selected_data.action_id]->perform(p_evt_handler, use_token ? get_access_token() : std::string(), selected_data.success_callback, selected_data.fail_callback, selected_data.input);
        process_action_queue_inner();
    }        
}

void UserAccountSession::enqueue_action(UserAccountActionID id, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input)
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        enqueue_action_inner(id, success_callback, fail_callback, input);
    }
}

// called from m_session_mutex protected code only!
void UserAccountSession::enqueue_action_inner(UserAccountActionID id, UserActionSuccessFn success_callback, UserActionFailFn fail_callback, const std::string& input)
{
        m_proccessing_enabled = true;
        m_action_queue.push({ id, success_callback, fail_callback, input });
}

void UserAccountSession::init_with_code(const std::string& code, const std::string& code_verifier)
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        // Data we have       
        const std::string REDIRECT_URI = "prusaslicer://login";
        std::string post_fields = "code=" + code +
            "&client_id=" + client_id() +
            "&grant_type=authorization_code" +
            "&redirect_uri=" + REDIRECT_URI +
            "&code_verifier="+ code_verifier;

        m_proccessing_enabled = true;
        // fail fn might be cancel_queue here
        m_priority_action_queue.push_back({ UserAccountActionID::USER_ACCOUNT_ACTION_CODE_FOR_TOKEN
            , std::bind(&UserAccountSession::token_success_callback, this, std::placeholders::_1)
            , std::bind(&UserAccountSession::code_exchange_fail_callback, this, std::placeholders::_1)
            , post_fields });
    }
}

void UserAccountSession::remove_from_queue(UserAccountActionID action_id)
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);

         auto it = std::find_if(
            std::begin(m_priority_action_queue), std::end(m_priority_action_queue),
            [action_id](const ActionQueueData& item) { return item.action_id == action_id; }
        );
        while (it != m_priority_action_queue.end())
        {
            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
            m_priority_action_queue.erase(it);
            it = std::find_if(
                std::begin(m_priority_action_queue), std::end(m_priority_action_queue),
                [action_id](const ActionQueueData& item) { return item.action_id == action_id; }
            );
        }
    }

}

void UserAccountSession::token_success_callback(const std::string& body)
{
    // No need to use lock m_session_mutex here

    // This is here to prevent performing refresh again until USER_ACCOUNT_ACTION_USER_ID_AFTER_TOKEN_SUCCESS is performed.
    // If refresh with stored token was enqueued during performing one we are in its success_callback,
    // It would fail and prevent USER_ID to write this tokens to store. 
    remove_from_queue(UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN);

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " Access token refreshed";
    // Data we need
    std::string access_token, refresh_token, shared_session_key;
    try {
        std::stringstream ss(body);
        pt::ptree ptree;
        pt::read_json(ss, ptree);

        const auto access_token_optional = ptree.get_optional<std::string>("access_token");
        const auto refresh_token_optional = ptree.get_optional<std::string>("refresh_token");
        const auto shared_session_key_optional = ptree.get_optional<std::string>("shared_session_key");
        if (access_token_optional)
            access_token = *access_token_optional;
        if (refresh_token_optional)
            refresh_token = *refresh_token_optional;
        if (shared_session_key_optional)
            shared_session_key = *shared_session_key_optional;
    }
    catch (const std::exception&) {
        std::string msg = "Could not parse server response after code exchange.";
        wxQueueEvent(p_evt_handler, new UserAccountFailEvent(EVT_UA_RESET, std::move(msg)));
        return;
    }
    int expires_in = Utils::get_exp_seconds(access_token);
    if (access_token.empty() || refresh_token.empty() || shared_session_key.empty() || expires_in <= 0) {
        // just debug msg, no need to translate
        std::string msg = GUI::format("Failed read tokens after POST.\nAccess token: %1%\nRefresh token: %2%\nShared session token: %3%\nbody: %4%", access_token, refresh_token, shared_session_key, body);
        {
            std::lock_guard<std::mutex> lock(m_credentials_mutex);
            m_access_token = std::string();
            m_refresh_token = std::string();
            m_shared_session_key = std::string();
            m_next_token_timeout = 0;
        }
        wxQueueEvent(p_evt_handler, new UserAccountFailEvent(EVT_UA_RESET, std::move(msg)));
        return;
    }

    if (!access_token.empty()) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ <<" access_token: " << access_token.substr(0,5) << "..." << access_token.substr(access_token.size()-5);
    } else {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ <<" access_token empty!";
    }
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<" access_token: " << access_token;
    //BOOST_LOG_TRIVIAL(info) << "refresh_token: " << refresh_token;
    //BOOST_LOG_TRIVIAL(info) << "shared_session_key: " << shared_session_key;
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<" expires_in: " << std::time(nullptr) + expires_in;
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        m_access_token = access_token;
        m_refresh_token = refresh_token;
        m_shared_session_key = shared_session_key;
        m_next_token_timeout = std::time(nullptr) + expires_in;
    }
    enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_USER_ID_AFTER_TOKEN_SUCCESS, nullptr, nullptr, {});
    wxQueueEvent(p_evt_handler, new UserAccountTimeEvent(EVT_UA_REFRESH_TIME, expires_in));
}

void UserAccountSession::set_tokens(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, long long expires_in) 
{
    if (!access_token.empty()) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ <<" access_token: " << access_token.substr(0,5) << "..." << access_token.substr(access_token.size()-5);
    } else {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ <<" access_token empty!";
    }
    
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        m_access_token = access_token;
        m_refresh_token = refresh_token;
        m_shared_session_key = shared_session_key;
        m_next_token_timeout = /*std::time(nullptr) +*/ expires_in;
    }
    long long exp = expires_in - std::time(nullptr);
    wxQueueEvent(p_evt_handler, new UserAccountTimeEvent(EVT_UA_REFRESH_TIME, exp));
}

void UserAccountSession::code_exchange_fail_callback(const std::string& body)
{
    
    BOOST_LOG_TRIVIAL(debug) << "Access token refresh failed, body: " << body;
    clear();
    cancel_queue();
    // Unlike refresh_fail_callback, no event was triggered so far, do it. (USER_ACCOUNT_ACTION_CODE_FOR_TOKEN does not send events)
    wxQueueEvent(p_evt_handler, new UserAccountFailEvent(EVT_UA_RESET, body));
}

void UserAccountSession::enqueue_test_with_refresh()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        // on test fail - try refresh
        m_proccessing_enabled = true;
        m_priority_action_queue.push_back({ UserAccountActionID::USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN, nullptr, std::bind(&UserAccountSession::enqueue_refresh, this, std::placeholders::_1), {} });
    }
}

void UserAccountSession::enqueue_refresh(const std::string& body)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    wxQueueEvent(p_evt_handler, new SimpleEvent(EVT_UA_ENQUEUED_REFRESH));
    std::string post_fields;
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        assert(!m_refresh_token.empty());
        post_fields = "grant_type=refresh_token"
                      "&client_id=" + client_id() +
                      "&refresh_token=" + m_refresh_token;
    }
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        m_priority_action_queue.push_back({ UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN
            , std::bind(&UserAccountSession::token_success_callback, this, std::placeholders::_1)
            , std::bind(&UserAccountSession::refresh_fail_callback, this, std::placeholders::_1)
            , post_fields });
    }
}

void UserAccountSession::refresh_fail_callback(const std::string& body)
{
    clear();
    cancel_queue();
    // No need to notify UI thread here
    // backtrace: load tokens -> TEST_TOKEN fail (access token bad) -> REFRESH_TOKEN fail (refresh token bad)
    // USER_ACCOUNT_ACTION_TEST_ACCESS_TOKEN triggers EVT_UA_FAIL, we need also RESET
    wxQueueEvent(p_evt_handler, new UserAccountFailEvent(EVT_UA_RESET, body));
}

void UserAccountSession::enqueue_refresh_race(const std::string refresh_token_from_store/* = std::string()*/)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    wxQueueEvent(p_evt_handler, new SimpleEvent(EVT_UA_ENQUEUED_REFRESH));
    std::string post_fields;
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        assert(!m_refresh_token.empty());
        post_fields = "grant_type=refresh_token"
                      "&client_id=" + client_id() +
                      "&refresh_token=" + (refresh_token_from_store.empty() ? m_refresh_token :refresh_token_from_store);
    }
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        m_priority_action_queue.push_back({ UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN
            , std::bind(&UserAccountSession::token_success_callback, this, std::placeholders::_1)
            , std::bind(&UserAccountSession::refresh_fail_soft_callback, this, std::placeholders::_1)
            , post_fields });
    }
}

void UserAccountSession::refresh_fail_soft_callback(const std::string& body)
{ 
    cancel_queue();
    wxQueueEvent(p_evt_handler, new UserAccountFailEvent(EVT_UA_RACE_LOST, body));
}

void UserAccountSession::cancel_queue()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        m_priority_action_queue.clear();
        while (!m_action_queue.empty()) {
            m_action_queue.pop();
        }
    }
}

}} // Slic3r::GUI