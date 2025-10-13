#include "UserAccountCommunication.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "format.hpp"
#include "../Utils/Http.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/string/split.hpp>
#include <boost/log/trivial.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/convert.hpp>
#include <curl/curl.h>
#include <string>

#include <iostream>
#include <random>
#include <algorithm>
#include <iterator>
#include <regex>
#include <iomanip>
#include <cstring>
#include <cstdint>

#if wxUSE_SECRETSTORE 
#include <wx/secretstore.h>
#endif

#ifdef WIN32
#include <wincrypt.h>
#endif // WIN32

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif

#ifdef __linux__
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include <fcntl.h>
#endif // __linux__



namespace fs = boost::filesystem;


namespace Slic3r {
namespace GUI {


namespace {

std::string get_code_from_message(const std::string& url_message)
{
    size_t pos = url_message.rfind("code=");
    std::string out;
    for (size_t i = pos + 5; i < url_message.size(); i++) {
        const char& c = url_message[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out+= c;
        else
            break;  
    }
    return out;
}

bool is_secret_store_ok()
{
#if wxUSE_SECRETSTORE 
    wxSecretStore store = wxSecretStore::GetDefault();
    wxString errmsg;
    if (!store.IsOk(&errmsg)) {
        BOOST_LOG_TRIVIAL(warning) << "wxSecretStore is not supported: " << errmsg;
        return false;
    }
    return true;
#else
    return false;
#endif
}
bool save_secret(const std::string& opt, const std::string& usr, const std::string& psswd)
{
#if wxUSE_SECRETSTORE 
    wxSecretStore store = wxSecretStore::GetDefault();
    wxString errmsg;
    if (!store.IsOk(&errmsg)) {
        std::string msg = GUI::format("%1% (%2%).", _u8L("This system doesn't support storing passwords securely"), errmsg);
        BOOST_LOG_TRIVIAL(error) << msg;
        //show_error(nullptr, msg);
        return false;
    }
    const wxString service = GUI::format_wxstr(L"%1%/PrusaAccount/%2%", SLIC3R_APP_NAME, opt);
    const wxString username = boost::nowide::widen(usr);
    const wxSecretValue password(boost::nowide::widen(psswd));
    if (!store.Save(service, username, password)) {
        std::string msg(_u8L("Failed to save credentials to the system password store."));
        BOOST_LOG_TRIVIAL(error) << msg;
        //show_error(nullptr, msg);
        return false;
    }
    return true;
#else
    BOOST_LOG_TRIVIAL(error) << "wxUSE_SECRETSTORE not supported. Cannot save password to the system store.";
    return false;
#endif // wxUSE_SECRETSTORE 
}
bool load_secret(const std::string& opt, std::string& usr, std::string& psswd)
{
#if wxUSE_SECRETSTORE
    wxSecretStore store = wxSecretStore::GetDefault();
    wxString errmsg;
    if (!store.IsOk(&errmsg)) {
        std::string msg = GUI::format("%1% (%2%).", _u8L("This system doesn't support storing passwords securely"), errmsg);
        BOOST_LOG_TRIVIAL(error) << msg;
        //show_error(nullptr, msg);
        return false;
    }
    const wxString service = GUI::format_wxstr(L"%1%/PrusaAccount/%2%", SLIC3R_APP_NAME, opt);
    wxString username;
    wxSecretValue password;
    if (!store.Load(service, username, password)) {
        std::string msg(_u8L("Failed to load credentials from the system password store."));
        BOOST_LOG_TRIVIAL(error) << msg;
        //show_error(nullptr, msg);
        return false;
    }
    usr = into_u8(username);
    psswd = into_u8(password.GetAsString());
    return true;
#else
    BOOST_LOG_TRIVIAL(error) << "wxUSE_SECRETSTORE not supported. Cannot load password from the system store.";
    return false;
#endif // wxUSE_SECRETSTORE 
}

#ifdef __linux__
void load_tokens_linux(UserAccountCommunication::StoreData& result)
{
        // Load refresh token from UserAccount.dat
        boost::filesystem::path source(boost::filesystem::path(Slic3r::data_dir()) / "UserAccount.dat") ;
        // since there was for a short period different file in use, if present, load it and delete it.
        boost::system::error_code ec;
        bool delete_after_read = false;
        if (!boost::filesystem::exists(source, ec) || ec) {
            source = boost::filesystem::path(Slic3r::data_dir()) / "UserAcountData.dat";
            ec.clear();            
            if (!boost::filesystem::exists(source, ec) || ec) {
                BOOST_LOG_TRIVIAL(error) << "UserAccount: Failed to read token - no datafile found.";
                return;
            }
            delete_after_read = true;
        }
        boost::nowide::ifstream stream(source.generic_string(), std::ios::in | std::ios::binary);
        if (!stream) {
            BOOST_LOG_TRIVIAL(error) << "UserAccount: Failed to read tokens from " << source;
            return;
        }
        std::string token_data;
        std::getline(stream, token_data);
        stream.close();
        if (delete_after_read) {
            ec.clear();
            if (!boost::filesystem::remove(source, ec) || ec) {
                BOOST_LOG_TRIVIAL(error) << "UserAccount: Failed to remove file " << source;
            }
        }

        // read data
        std::vector<std::string> token_list;
        boost::split(token_list, token_data, boost::is_any_of("|"), boost::token_compress_off);
        assert(token_list.empty() || token_list.size() == 5);
        if (token_list.size() < 5) {
            BOOST_LOG_TRIVIAL(error) << "Size of read secrets is only: " << token_list.size() << " (expected 5). Data: " << token_data;
        }
        result.access_token = token_list.size() > 0 ? token_list[0] : std::string();
        result.refresh_token = token_list.size() > 1 ? token_list[1] : std::string();
        result.next_timeout = token_list.size() > 2 ? token_list[2] : std::string();
        result.master_pid =  token_list.size() > 3 ? token_list[3] : std::string();
        result.shared_session_key = token_list.size() > 4 ? token_list[4] : std::string();
}
bool concurrent_write_file(const std::string& secret_to_store, const boost::filesystem::path& filename)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    // Open the file
    int fd = open(filename.string().c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        BOOST_LOG_TRIVIAL(error)  << "Unable to open store file " << filename << ": " << strerror(errno);
        return false;
    }
    // Close the file when the guard dies
    Slic3r::ScopeGuard sg_fd([fd]() { 
        close(fd); 
        BOOST_LOG_TRIVIAL(debug) << "Closed file.";
    });

    // Configure the lock
    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;  // Write lock
    lock.l_whence = SEEK_SET;  // Lock from the start of the file
    lock.l_start = 0;
    lock.l_len = 0;  // 0 means lock the entire file

    // Try to acquire the lock
    BOOST_LOG_TRIVIAL(debug) << "Waiting to acquire lock on file: " << filename;
    if (fcntl(fd, F_SETLKW, &lock) == -1) {
        BOOST_LOG_TRIVIAL(error) << "Unable to acquire lock: " << strerror(errno);
        return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "Lock acquired on file: " << filename;
    
    Slic3r::ScopeGuard sg_lock([&lock, fd, filename]() {
        // Release the lock when guard dies.
        lock.l_type = F_UNLCK;  // Unlock the file
       if (fcntl(fd, F_SETLK, &lock) == -1) {
            BOOST_LOG_TRIVIAL(error)  << "Unable to release lock ("<< filename <<"): " << strerror(errno);
        } else {
            BOOST_LOG_TRIVIAL(debug) << "Lock released on file: " << filename;
        }
    });

    // Write content to the file
    if (write(fd, secret_to_store.c_str(), strlen(secret_to_store.c_str())) == -1) {
        BOOST_LOG_TRIVIAL(error)  << "Unable to write to file: " << strerror(errno);
        return false;
    }
    BOOST_LOG_TRIVIAL(debug) << "Content written to file.";
    return true;
}
#endif //__linux__
}

UserAccountCommunication::UserAccountCommunication(wxEvtHandler* evt_handler, AppConfig* app_config)
    : wxEvtHandler()
    , m_evt_handler(evt_handler)
    , m_app_config(app_config)
    , m_polling_timer(std::make_unique<wxTimer>(this))
    , m_token_timer(std::make_unique<wxTimer>(this))
    , m_slave_read_timer(std::make_unique<wxTimer>(this))
    , m_after_race_lost_timer(std::make_unique<wxTimer>(this))
{
    Bind(wxEVT_TIMER, &UserAccountCommunication::on_token_timer, this, m_token_timer->GetId());
    Bind(wxEVT_TIMER, &UserAccountCommunication::on_polling_timer, this, m_polling_timer->GetId());
    Bind(wxEVT_TIMER, &UserAccountCommunication::on_slave_read_timer, this, m_slave_read_timer->GetId());
    Bind(wxEVT_TIMER, &UserAccountCommunication::on_after_race_lost_timer, this, m_after_race_lost_timer->GetId());

    StoreData stored_data;
    read_stored_data(stored_data);

    long long next_timeout_long = stored_data.next_timeout.empty() ? 0 : std::stoll(stored_data.next_timeout);
    long long remain_time = next_timeout_long - std::time(nullptr);
    if (remain_time <= 0) {
        stored_data.access_token.clear();
    } else {
        set_refresh_time((int)remain_time);
    }
    if (!stored_data.access_token.empty()) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ <<" access_token: " << stored_data.access_token.substr(0,5) << "..." << stored_data.access_token.substr(stored_data.access_token.size()-5);
    } else {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ <<" access_token empty!";
    }
    bool has_token = !stored_data.refresh_token.empty();
    m_session = std::make_unique<UserAccountSession>(evt_handler, stored_data.access_token, stored_data.refresh_token, stored_data.shared_session_key, next_timeout_long, m_app_config->get_bool("connect_polling"));
    init_session_thread();
    // perform login at the start, but only with tokens
    if (has_token) {
        do_login();
    }
}

UserAccountCommunication::~UserAccountCommunication() 
{
    m_token_timer->Stop();
    m_polling_timer->Stop();

    if (m_thread.joinable()) {
        // Stop the worker thread, if running.
        {
            // Notify the worker thread to cancel wait on detection polling.
            std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
            m_thread_stop = true;
        }
        m_thread_stop_condition.notify_all();
        // Wait for the worker thread to stop
        m_thread.join();
    }
}

void UserAccountCommunication::set_username(const std::string& username, bool store)
{
    m_username = username;
    if (!store && !username.empty()) {
        return;
    }
    {
        //BOOST_LOG_TRIVIAL(debug) << __FUNCTION__  << " empty: " << username.empty();
        std::string at = m_session->get_access_token();
        if (!at.empty())
            at = at.substr(0,5) + "..." + at.substr(at.size()-5);
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ <<" access_token: " << (username.empty() ? "" : at);
        if (is_secret_store_ok()) {
            std::string tokens = "|||";
            if (m_remember_session && !username.empty()) {
                tokens = m_session->get_access_token() +
                "|" + m_session->get_refresh_token() +
                "|" + std::to_string(m_session->get_next_token_timeout()) +
                "|" + std::to_string(get_current_pid());
            }
            if (!save_secret("tokens", m_session->get_shared_session_key(), tokens)) {
                BOOST_LOG_TRIVIAL(error) << "Failed to write tokens to the secret store.";
            }
        } else {
#ifdef __linux__
            // If we can't store the tokens in secret store, store them in file with chmod 600
            boost::filesystem::path target(boost::filesystem::path(Slic3r::data_dir()) / "UserAccount.dat") ;
            std::string data = "||||";
            if (m_remember_session && !username.empty()) {
                data = m_session->get_access_token() +
                "|" + m_session->get_refresh_token() +
                "|" + std::to_string(m_session->get_next_token_timeout()) +
                "|" + std::to_string(get_current_pid()) +
                "|" + m_session->get_shared_session_key();
            }

            FILE* file; 
            static const auto perms = boost::filesystem::owner_read | boost::filesystem::owner_write;   // aka 600
            
            boost::system::error_code ec;
            boost::filesystem::permissions(target, perms, ec);
            if (ec)
                BOOST_LOG_TRIVIAL(debug) << "UserAccount: boost::filesystem::permisions before write error message (this could be irrelevant message based on file system): " << ec.message();
            ec.clear();

            if (!concurrent_write_file(data, target)) {
                BOOST_LOG_TRIVIAL(error) << "Failed to store secret.";
            }

            boost::filesystem::permissions(target, perms, ec);
            if (ec)
                BOOST_LOG_TRIVIAL(debug) << "UserAccount: boost::filesystem::permisions after write error message (this could be irrelevant message based on file system): " << ec.message();
#else
            BOOST_LOG_TRIVIAL(error) << "Failed to write tokens to the secret store: Store is not ok.";
#endif
        }
    }
}

void UserAccountCommunication::set_remember_session(bool b)
{ 
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    m_remember_session = b;
    // tokens needs to be stored or deleted
    set_username(m_username, true);
}

std::string UserAccountCommunication::get_access_token()
{

    return m_session->get_access_token();

}

std::string UserAccountCommunication::get_shared_session_key()
{

    return m_session->get_shared_session_key();

}

void UserAccountCommunication::set_polling_enabled(bool enabled)
{
    // Here enabled sets to USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS so it gets full list on first,
    // than it should change inside session to USER_ACCOUNT_ACTION_CONNECT_STATUS
    return m_session->set_polling_action(enabled ? UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS : UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY);
}

void UserAccountCommunication::on_uuid_map_success()
{

    return m_session->set_polling_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS);
    
}

// Generates and stores Code Verifier - second call deletes previous one.
wxString UserAccountCommunication::generate_login_redirect_url()
{
    auto& sc = Utils::ServiceConfig::instance();
    const std::string AUTH_HOST = sc.account_url();
    const std::string CLIENT_ID = client_id();
    const std::string REDIRECT_URI = "prusaslicer://login";
    CodeChalengeGenerator ccg;
    m_code_verifier = ccg.generate_verifier();
    std::string code_challenge = ccg.generate_challenge(m_code_verifier);
    wxString language = GUI::wxGetApp().current_language_code();
    language = language.SubString(0, 1);
    BOOST_LOG_TRIVIAL(info) << "code verifier: " << m_code_verifier;
    BOOST_LOG_TRIVIAL(info) << "code challenge: " << code_challenge;

    wxString url = GUI::format_wxstr(L"%1%/o/authorize/?embed=1&client_id=%2%&response_type=code&code_challenge=%3%&code_challenge_method=S256&scope=basic_info&redirect_uri=%4%&language=%5%", AUTH_HOST, CLIENT_ID, code_challenge, REDIRECT_URI, language);
    return url;
}
wxString UserAccountCommunication::get_login_redirect_url(const std::string& service/* = std::string()*/) const
{
    auto& sc = Utils::ServiceConfig::instance();
    const std::string AUTH_HOST = sc.account_url();
    const std::string CLIENT_ID = client_id();
    const std::string REDIRECT_URI = "prusaslicer://login";
    CodeChalengeGenerator ccg;
    std::string code_challenge = ccg.generate_challenge(m_code_verifier);
    wxString language = GUI::wxGetApp().current_language_code();
    language = language.SubString(0, 1);

    std::string params = GUI::format("embed=1&client_id=%1%&response_type=code&code_challenge=%2%&code_challenge_method=S256&scope=basic_info&redirect_uri=%3%&language=%4%", CLIENT_ID, code_challenge, REDIRECT_URI, language);
    params = Http::url_encode(params);
    wxString url = GUI::format_wxstr(L"%1%/login/%2%?next=/o/authorize/?%3%", AUTH_HOST, service, params);
    return url;
}
void UserAccountCommunication::login_redirect()
{
    wxString url1 = generate_login_redirect_url();
    wxString url2 = url1 + L"&choose_account=1";
    wxQueueEvent(m_evt_handler,new OpenPrusaAuthEvent(GUI::EVT_OPEN_PRUSAAUTH, {std::move(url1), std::move(url2)}));
}

bool UserAccountCommunication::is_logged() const
{
    return !m_username.empty();
}
void UserAccountCommunication::do_login()
{
    if (!m_session->is_initialized()) {
        login_redirect();
    } else { 
        m_session->enqueue_test_with_refresh();
    }
    wakeup_session_thread();
}
void UserAccountCommunication::do_logout()
{
    do_clear();
    wxQueueEvent(m_evt_handler, new UserAccountSuccessEvent(GUI::EVT_UA_LOGGEDOUT, {}));
}

void UserAccountCommunication::do_clear()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    m_session->clear();
    set_username({}, true);
    m_token_timer->Stop();
    m_slave_read_timer->Stop();
    m_after_race_lost_timer->Stop();
    m_next_token_refresh_at = 0;
}

void UserAccountCommunication::on_login_code_recieved(const std::string& url_message)
{
    const std::string code = get_code_from_message(url_message);
    m_session->init_with_code(code, m_code_verifier);
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_connect_printer_models_action()
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect Printer Models connection failed - Not Logged in.";
        return;
    }
    m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS, nullptr, nullptr, {});
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_connect_status_action()
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect Status endpoint connection failed - Not Logged in.";
        return;
    }
    m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS, nullptr, nullptr, {});
    wakeup_session_thread();
}
void UserAccountCommunication::enqueue_test_connection()
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect test endpoint connection failed - Not Logged in.";
        return;
    }
    m_session->enqueue_test_with_refresh();
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_avatar_old_action(const std::string& url)
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect avatar endpoint connection failed - Not Logged in.";
        return;
    }
    m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR_OLD, nullptr, nullptr, url);
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_avatar_new_action(const std::string& url)
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
        return;
    }
    m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR_NEW, nullptr, nullptr, url);
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_id_action()
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect id endpoint connection failed - Not Logged in.";
        return;
    }
    m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_USER_ID, nullptr, nullptr, {});
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_printer_data_action(const std::string& uuid)
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
        return;
    }
    m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID, nullptr, nullptr, uuid);   
    wakeup_session_thread();
}
   
void UserAccountCommunication::enqueue_refresh()
{
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
        return;
    }
    if (m_session->is_enqueued(UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN)) {
        BOOST_LOG_TRIVIAL(debug) << "User Account: Token refresh already enqueued, skipping...";
        return;
    }
    m_session->enqueue_refresh({});
    wakeup_session_thread();
}

void UserAccountCommunication::init_session_thread()
{
    assert(m_polling_timer);
    m_polling_timer->Start(10000);
    m_thread = std::thread([this]() {
        for (;;) {
            {
                std::unique_lock<std::mutex> lck(m_thread_stop_mutex);      
                m_thread_stop_condition.wait_for(lck, std::chrono::seconds(88888), [this] { return m_thread_stop || m_thread_wakeup; });
            }
            if (m_thread_stop)
                // Stop the worker thread.
                break;
            // Do not process_action_queue if window is not active and thread was not forced to wakeup
            if (!m_window_is_active && !m_thread_wakeup) {
                continue;
            }
            m_thread_wakeup = false;
            m_session->process_action_queue();
        }
    });
}

void UserAccountCommunication::on_activate_app(bool active)
{
    {
        std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
        m_window_is_active = active;
    }
    auto now = std::time(nullptr);
    //BOOST_LOG_TRIVIAL(info) << "UserAccountCommunication activate: active " << active;
#ifndef _NDEBUG
    // constexpr auto refresh_threshold = 110 * 60;
    constexpr auto refresh_threshold = 60;
#else
    constexpr auto refresh_threshold = 60;
#endif
    if (active && m_next_token_refresh_at > 0 && m_next_token_refresh_at - now < refresh_threshold) {
        // Commented during implementation of sharing access token among instances - TODO
        BOOST_LOG_TRIVIAL(debug) << " Requesting refresh when app was activated: next token refresh is at " <<  m_next_token_refresh_at - now;
        request_refresh();
        return;
    }
    // When no token timers are running but we have token -> refresh it.
    if (active && m_next_token_refresh_at > 0 && m_token_timer->IsRunning() && m_slave_read_timer->IsRunning() && m_after_race_lost_timer->IsRunning()) {
        BOOST_LOG_TRIVIAL(debug) << " Requesting refresh when app was activated when no timers are running, next token refresh is at " <<  m_next_token_refresh_at - now;
        request_refresh();
        return;
    }
}

void UserAccountCommunication::wakeup_session_thread()
{
    //BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    {
        std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
        m_thread_wakeup = true;
    }
    m_thread_stop_condition.notify_all();
}

void UserAccountCommunication::set_refresh_time(int seconds)
{
    assert(m_token_timer);
    m_token_timer->Stop();
    m_last_token_duration_seconds = seconds;
    const auto prior_expiration_secs = std::max(seconds / 24, 10);
    int milliseconds = std::max((seconds - prior_expiration_secs) * 1000, 1000);
    m_next_token_refresh_at = std::time(nullptr) + milliseconds / 1000;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << milliseconds / 1000;
    m_token_timer->StartOnce(milliseconds);
}

void UserAccountCommunication::read_stored_data(UserAccountCommunication::StoreData& result)
{
    if (is_secret_store_ok()) {
        std::string key0, tokens;
        if (load_secret("tokens", key0, tokens)) {
            std::vector<std::string> token_list;
            boost::split(token_list, tokens, boost::is_any_of("|"), boost::token_compress_off);
            assert(token_list.empty() || token_list.size() == 4);
            if (token_list.size() < 3) {
                BOOST_LOG_TRIVIAL(error) << "Size of read secrets is only: " << token_list.size() << " (expected 4). Data: " << tokens;
            }
            result.access_token = token_list.size() > 0 ? token_list[0] : std::string();
            result.refresh_token = token_list.size() > 1 ? token_list[1] : std::string();
            result.next_timeout = token_list.size() > 2 ? token_list[2] : std::string();
            result.master_pid =  token_list.size() > 3 ? token_list[3] : std::string();
        }
        result.shared_session_key = key0;
    } else {
#ifdef __linux__
        load_tokens_linux(result);
#endif
    }
}

void UserAccountCommunication::request_refresh()
{
    // This function is called when Printables requests new token - same token as we have now wont do.
    // Or from UserAccountCommunication::on_activate_app(true) when current token has too small refresh or is dead
    // See if there is different token stored, if not - proceed to T3 (there might be more than 1 app doing this).
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    if (m_token_timer->IsRunning()) {
        m_token_timer->Stop();
    } 
    if (m_slave_read_timer->IsRunning()) {
        m_slave_read_timer->Stop();
    }
    if (m_after_race_lost_timer->IsRunning()) {
        m_after_race_lost_timer->Stop();
    }
    
    std::string current_access_token = m_session->get_access_token();
    StoreData stored_data;
    read_stored_data(stored_data);
    if (stored_data.refresh_token.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Store is empty - logging out.";
        do_logout();
        return;
    }

    // Here we need to count with situation when token was renewed in m_session but was not yet stored.
    // Then store token is not valid - it should has erlier expiration
    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : std::stoll(stored_data.next_timeout) - std::time(nullptr);
    BOOST_LOG_TRIVIAL(debug) << "Compare " <<  expires_in_second << " vs " << m_next_token_refresh_at - std::time(nullptr) << (stored_data.access_token != current_access_token ? " not same" : " same");
    if (stored_data.access_token != current_access_token && expires_in_second > 0 && expires_in_second > m_next_token_refresh_at - std::time(nullptr)) {
        BOOST_LOG_TRIVIAL(debug) << "Found usable token. Expires in " << expires_in_second;
        set_tokens(stored_data);
    } else {
        BOOST_LOG_TRIVIAL(debug) << "No new token";
        enqueue_refresh_race(stored_data.refresh_token);
    }
}

void UserAccountCommunication::on_token_timer(wxTimerEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " T1";
    // Read PID from current stored token and decide if master / slave

    std::string my_pid = std::to_string(get_current_pid());
    StoreData stored_data;
    read_stored_data(stored_data);

    if (stored_data.refresh_token.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Store is empty - logging out.";
        do_logout();
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : std::stoll(stored_data.next_timeout) - std::time(nullptr);
    if (my_pid == stored_data.master_pid) {
        enqueue_refresh(); 
        return;
    } 
    // token could be either already new -> we want to start using it now
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second >= 0 && expires_in_second > prior_expiration_secs) {
        BOOST_LOG_TRIVIAL(debug) << "Current token has different PID - expiration is " << expires_in_second << " while longest expected was " << prior_expiration_secs << ". Using this token.";
        set_tokens(stored_data);
        return;
    } 
    // or yet to be renewed -> we should wait to give time to master to renew it
    if (expires_in_second >= 0) {
        BOOST_LOG_TRIVIAL(debug) << "Current token has different PID - waiting " << expires_in_second / 2;
        m_slave_read_timer->StartOnce((expires_in_second / 2) * 1000);
        return;
    }
    // or expired -> renew now.
    BOOST_LOG_TRIVIAL(debug) << "Current token has different PID and is expired.";
    enqueue_refresh_race(stored_data.refresh_token);
}
void UserAccountCommunication::on_slave_read_timer(wxTimerEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " T2";
    std::string current_access_token = m_session->get_access_token();
    StoreData stored_data;
    read_stored_data(stored_data);

    if (stored_data.refresh_token.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Store is empty - logging out.";
        do_logout();
        return;
    }
    
    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : std::stoll(stored_data.next_timeout) - std::time(nullptr);
    if (stored_data.access_token != current_access_token) {
        // consider stored_data as renewed token from master
        BOOST_LOG_TRIVIAL(debug) << "Token in store seems to be new - using it.";
        set_tokens(stored_data);   
        return;
    }
    if (stored_data.access_token != current_access_token) {
        // token is expired
        BOOST_LOG_TRIVIAL(debug) << "Token in store seems to be new but expired - refreshing now.";
        enqueue_refresh_race(stored_data.refresh_token);
        return;
    }
    BOOST_LOG_TRIVIAL(debug) <<"No new token, enqueueing refresh (race expected).";
    enqueue_refresh_race();
}

void UserAccountCommunication::enqueue_refresh_race(const std::string refresh_token_from_store/* = std::string()*/)
{
    BOOST_LOG_TRIVIAL(debug) <<  __FUNCTION__ << " T3";
    if (!m_session->is_initialized()) {
        BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
        return;
    }
    if (refresh_token_from_store.empty() && m_session->is_enqueued(UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN)) {
        BOOST_LOG_TRIVIAL(error) <<  __FUNCTION__ << " Token refresh already enqueued, skipping...";
        return;
    }
    // At this point, last master did not renew the tokens, behave like master
    m_session->enqueue_refresh_race();
    wakeup_session_thread();
}

void UserAccountCommunication::on_race_lost()
{
    BOOST_LOG_TRIVIAL(debug) <<  __FUNCTION__ << " T4";
    // race from on_slave_read_timer has been lost
    // other instance was faster to renew tokens so refresh token from this app was denied (invalid grant)
    // we should read the other token now.
    std::string current_access_token = m_session->get_access_token();
    StoreData stored_data;
    read_stored_data(stored_data);

    if (stored_data.refresh_token.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Store is empty - logging out.";
        do_logout();
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : std::stoll(stored_data.next_timeout) - std::time(nullptr);
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second > 0 && expires_in_second > prior_expiration_secs) {
        BOOST_LOG_TRIVIAL(debug) << "Token is alive - using it.";
        set_tokens(stored_data);
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << "No suitable token found waiting " << std::max((expires_in_second / 2), (long long)2);
    m_after_race_lost_timer->StartOnce(std::max((expires_in_second / 2) * 1000, (long long)2000));   
}

void UserAccountCommunication::on_after_race_lost_timer(wxTimerEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " T5";

    std::string current_access_token = m_session->get_access_token();
    StoreData stored_data;
    read_stored_data(stored_data);

    if (stored_data.refresh_token.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Store is empty - logging out.";
        do_logout();
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : std::stoll(stored_data.next_timeout) - std::time(nullptr);
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second > 0 && expires_in_second > prior_expiration_secs) {
       BOOST_LOG_TRIVIAL(debug) << "Token is alive - using it.";
       set_tokens(stored_data);
       return;
    } 
    BOOST_LOG_TRIVIAL(warning) << "No new token is stored - This is error state. Logging out.";
    do_logout();
}

void UserAccountCommunication::set_tokens(const StoreData store_data)
{
    if (m_token_timer->IsRunning()) {
        m_token_timer->Stop();
    } 
    if (m_slave_read_timer->IsRunning()) {
        m_slave_read_timer->Stop();
    }
    if (m_after_race_lost_timer->IsRunning()) {
        m_after_race_lost_timer->Stop();
    }

    long long next = store_data.next_timeout.empty() ? 0 : std::stoll(store_data.next_timeout);
    m_session->set_tokens(store_data.access_token, store_data.refresh_token, store_data.shared_session_key, next);
    enqueue_id_action();
}

void UserAccountCommunication::on_store_read_request()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__;
    StoreData stored_data;
    read_stored_data(stored_data);

    if (stored_data.refresh_token.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "Store is empty - logging out.";
        do_logout();
        return;
    }
    
    std::string currrent_access_token = m_session->get_access_token();
    if (currrent_access_token == stored_data.access_token)
    {
        BOOST_LOG_TRIVIAL(debug) << "Current token is up to date.";
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : std::stoll(stored_data.next_timeout) - std::time(nullptr);
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second > 0 /*&& expires_in_second > prior_expiration_secs*/) {
       BOOST_LOG_TRIVIAL(debug) << "Token is alive - using it.";
       set_tokens(stored_data);
       return;
    } else {
        BOOST_LOG_TRIVIAL(warning) << "Token read from store is expired!";
    }
}

void UserAccountCommunication::on_polling_timer(wxTimerEvent& evt)
{

    if (!m_window_is_active) {
        return;
    }
    wakeup_session_thread();
}

std::string CodeChalengeGenerator::generate_challenge(const std::string& verifier)
{
    std::string code_challenge;
    try
    {
        code_challenge = sha256(verifier);
        code_challenge = base64_encode(code_challenge);
    }
    catch (const std::exception& e)
    {
        BOOST_LOG_TRIVIAL(error) << "Code Challenge Generator failed: " << e.what();
    }
    assert(!code_challenge.empty());
    return code_challenge;
    
}
std::string CodeChalengeGenerator::generate_verifier()
{
    size_t length = 40;
    std::string code_verifier = generate_code_verifier(length);
    assert(code_verifier.size() == length);
    return code_verifier;
}
std::string CodeChalengeGenerator::base64_encode(const std::string& input)
{
    std::string output;
    output.resize(boost::beast::detail::base64::encoded_size(input.size()));
    boost::beast::detail::base64::encode(&output[0], input.data(), input.size());
    // save encode - replace + and / with - and _
    std::replace(output.begin(), output.end(), '+', '-');
    std::replace(output.begin(), output.end(), '/', '_');
    // remove last '=' sign 
    while (output.back() == '=')
        output.pop_back();
    return output;
}
std::string CodeChalengeGenerator::generate_code_verifier(size_t length)
{
    const std::string                   chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device                  rd;
    std::mt19937                        gen(rd());
    std::uniform_int_distribution<int>  distribution(0, chars.size() - 1);
    std::string                         code_verifier;
    for (size_t i = 0; i < length; ++i) {
        code_verifier += chars[distribution(gen)];
    }
    return code_verifier;
}

#ifdef WIN32
std::string CodeChalengeGenerator::sha256(const std::string& input)
{
    HCRYPTPROV          prov_handle = NULL;
    HCRYPTHASH          hash_handle = NULL;
    DWORD               hash_size = 0;
    DWORD               buffer_size = sizeof(DWORD);
    std::string         output;

    if (!CryptAcquireContext(&prov_handle, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        throw std::exception("CryptAcquireContext failed.");
    }
    if (!CryptCreateHash(prov_handle, CALG_SHA_256, 0, 0, &hash_handle)) {
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptCreateHash failed.");
    }
    if (!CryptHashData(hash_handle, reinterpret_cast<const BYTE*>(input.c_str()), input.length(), 0)) {
        CryptDestroyHash(hash_handle);
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptCreateHash failed.");
    }
    if (!CryptGetHashParam(hash_handle, HP_HASHSIZE, reinterpret_cast<BYTE*>(&hash_size), &buffer_size, 0)) {
        CryptDestroyHash(hash_handle);
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptGetHashParam HP_HASHSIZE failed.");
    }
    output.resize(hash_size);
    if (!CryptGetHashParam(hash_handle, HP_HASHVAL, reinterpret_cast<BYTE*>(&output[0]), &hash_size, 0)) {
        CryptDestroyHash(hash_handle);
        CryptReleaseContext(prov_handle, 0);
        throw std::exception("CryptGetHashParam HP_HASHVAL failed.");
    }
    return output;
}
#elif __APPLE__
std::string CodeChalengeGenerator::sha256(const std::string& input) {
    // Initialize the context
    CC_SHA256_CTX sha256;
    CC_SHA256_Init(&sha256);

    // Update the context with the input data
    CC_SHA256_Update(&sha256, input.c_str(), static_cast<CC_LONG>(input.length()));

    // Finalize the hash and retrieve the result
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &sha256);

    return std::string(reinterpret_cast<char*>(digest), CC_SHA256_DIGEST_LENGTH);
}
#else
std::string CodeChalengeGenerator::sha256(const std::string& input) {
    EVP_MD_CTX* mdctx;
    const EVP_MD* md;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen;

    md = EVP_sha256();
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, input.c_str(), input.length());
    EVP_DigestFinal_ex(mdctx, digest, &digestLen);
    EVP_MD_CTX_free(mdctx);

    return std::string(reinterpret_cast<char*>(digest), digestLen);
}
#endif // __linux__
}} // Slic3r::GUI
