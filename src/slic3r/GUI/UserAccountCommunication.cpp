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
void load_refresh_token_linux(std::string& refresh_token)
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
            BOOST_LOG_TRIVIAL(error) << "UserAccount: Failed to read token from " << source;
            return;
        }
        std::getline(stream, refresh_token);
        stream.close();
        if (delete_after_read) {
            ec.clear();
            if (!boost::filesystem::remove(source, ec) || ec) {
                BOOST_LOG_TRIVIAL(error) << "UserAccount: Failed to remove file " << source;
            }

        }
}
#endif //__linux__
}

UserAccountCommunication::UserAccountCommunication(wxEvtHandler* evt_handler, AppConfig* app_config)
    : wxEvtHandler()
    , m_evt_handler(evt_handler)
    , m_app_config(app_config)
    , m_polling_timer(new wxTimer(this))
    , m_token_timer(new wxTimer(this))
{
    Bind(wxEVT_TIMER, &UserAccountCommunication::on_token_timer, this, m_token_timer->GetId());
    Bind(wxEVT_TIMER, &UserAccountCommunication::on_polling_timer, this, m_polling_timer->GetId());

    std::string access_token, refresh_token, shared_session_key, next_timeout;
    if (is_secret_store_ok()) {
        std::string key0, key1, key2, tokens;
        if (load_secret("tokens", key0, tokens)) {
            std::vector<std::string> token_list;
            boost::split(token_list, tokens, boost::is_any_of("|"), boost::token_compress_off);
            assert(token_list.empty() || token_list.size() == 3);
            access_token = token_list.size() > 0 ? token_list[0] : std::string();
            refresh_token = token_list.size() > 1 ? token_list[1] : std::string();
            next_timeout = token_list.size() > 2 ? token_list[2] : std::string();
        } else {
            load_secret("access_token", key0, access_token);
            load_secret("refresh_token", key1, refresh_token);
            load_secret("access_token_timeout", key2, next_timeout);
            assert(key0 == key1);
        }
        shared_session_key = key0;

    } else {
#ifdef __linux__
        load_refresh_token_linux(refresh_token);
#endif
    }
    long long next = next_timeout.empty() ? 0 : std::stoll(next_timeout);
    long long remain_time = next - std::time(nullptr);
    if (remain_time <= 0) {
        access_token.clear();
    } else {
        set_refresh_time((int)remain_time);
    }
    bool has_token = !refresh_token.empty();
    m_session = std::make_unique<UserAccountSession>(evt_handler, access_token, refresh_token, shared_session_key, m_app_config->get_bool("connect_polling"));
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

void UserAccountCommunication::set_username(const std::string& username)
{
    m_username = username;
    {
        // We don't need mutex lock here, as credentials are guarded by own mutex in m_session
        //std::lock_guard<std::mutex> lock(m_session_mutex);
        if (is_secret_store_ok()) {
            std::string tokens;
            if (m_remember_session) {
                tokens = m_session->get_access_token() +
                    "|" + m_session->get_refresh_token() +
                    "|" + std::to_string(m_session->get_next_token_timeout());
            }
            save_secret("tokens", m_session->get_shared_session_key(), tokens);
        }
        else {
#ifdef __linux__
            // If we can't store the tokens in secret store, store them in file with chmod 600
            boost::filesystem::path target(boost::filesystem::path(Slic3r::data_dir()) / "UserAccount.dat") ;
            std::string data = m_session->get_refresh_token();
            FILE* file; 
            static const auto perms = boost::filesystem::owner_read | boost::filesystem::owner_write;   // aka 600
            
            boost::system::error_code ec;
            boost::filesystem::permissions(target, perms, ec);
            if (ec)
                BOOST_LOG_TRIVIAL(debug) << "UserAccount: boost::filesystem::permisions before write error message (this could be irrelevant message based on file system): " << ec.message();
            ec.clear();

            file = boost::nowide::fopen(target.generic_string().c_str(), "wb");
            if (file == NULL) {
                BOOST_LOG_TRIVIAL(error) << "UserAccount: Failed to open file to store token: " << target;
                return;
            }
            fwrite(data.c_str(), 1, data.size(), file);
            fclose(file);

            boost::filesystem::permissions(target, perms, ec);
            if (ec)
                BOOST_LOG_TRIVIAL(debug) << "UserAccount: boost::filesystem::permisions after write error message (this could be irrelevant message based on file system): " << ec.message();
#endif
        }
    }
}

void UserAccountCommunication::set_remember_session(bool b)
{ 
    m_remember_session = b;
    // tokens needs to be stored or deleted
    set_username(m_username);
}

std::string UserAccountCommunication::get_access_token()
{
    {
        // We don't need mutex lock here, as credentials are guarded by own mutex in m_session
        //std::lock_guard<std::mutex> lock(m_session_mutex);
        return m_session->get_access_token();
    }
}

std::string UserAccountCommunication::get_shared_session_key()
{
    {
        // We don't need mutex lock here, as credentials are guarded by own mutex in m_session
        //std::lock_guard<std::mutex> lock(m_session_mutex);
        return m_session->get_shared_session_key();
    }
}

void UserAccountCommunication::set_polling_enabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        return m_session->set_polling_action(enabled ? UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS : UserAccountActionID::USER_ACCOUNT_ACTION_DUMMY);
    }
}

void UserAccountCommunication::on_uuid_map_success()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        return m_session->set_polling_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS);
    }
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
    std::string code_challenge = ccg.generate_chalenge(m_code_verifier);
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
    std::string code_challenge = ccg.generate_chalenge(m_code_verifier);
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

bool UserAccountCommunication::is_logged()
{
    return !m_username.empty();
}
void UserAccountCommunication::do_login()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_session->is_initialized()) {
            login_redirect();
        } else { 
            m_session->enqueue_test_with_refresh();
        }
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
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        m_session->clear();
    }
    set_username({});
    m_token_timer->Stop();
}

void UserAccountCommunication::on_login_code_recieved(const std::string& url_message)
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        const std::string code = get_code_from_message(url_message);
        m_session->init_with_code(code, m_code_verifier);
    }
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_connect_printer_models_action()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_session->is_initialized()) {
            BOOST_LOG_TRIVIAL(error) << "Connect Printer Models connection failed - Not Logged in.";
            return;
        }
        m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_PRINTER_MODELS, nullptr, nullptr, {});
    }
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_connect_status_action()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_session->is_initialized()) {
            BOOST_LOG_TRIVIAL(error) << "Connect Status endpoint connection failed - Not Logged in.";
            return;
        }
        m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_STATUS, nullptr, nullptr, {});
    }
    wakeup_session_thread();
}
void UserAccountCommunication::enqueue_test_connection()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_session->is_initialized()) {
            BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
            return;
        }
        m_session->enqueue_test_with_refresh();
    }
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_avatar_action(const std::string& url)
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_session->is_initialized()) {
            BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
            return;
        }
        m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_AVATAR, nullptr, nullptr, url);
    }
    wakeup_session_thread();
}

void UserAccountCommunication::enqueue_printer_data_action(const std::string& uuid)
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_session->is_initialized()) {
            BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
            return;
        }
        m_session->enqueue_action(UserAccountActionID::USER_ACCOUNT_ACTION_CONNECT_DATA_FROM_UUID, nullptr, nullptr, uuid);
    }
    wakeup_session_thread();
}
void UserAccountCommunication::enqueue_refresh()
{
    {
        std::lock_guard<std::mutex> lock(m_session_mutex);
        if (!m_session->is_initialized()) {
            BOOST_LOG_TRIVIAL(error) << "Connect Printers endpoint connection failed - Not Logged in.";
            return;
        }
        if (m_session->is_enqueued(UserAccountActionID::USER_ACCOUNT_ACTION_REFRESH_TOKEN)) {
            BOOST_LOG_TRIVIAL(debug) << "User Account: Token refresh already enqueued, skipping...";
            return;
        }
        m_session->enqueue_refresh({});
    }
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
            {
                std::lock_guard<std::mutex> lock(m_session_mutex);
                m_session->process_action_queue();
            }
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
    BOOST_LOG_TRIVIAL(info) << "UserAccountCommunication activate: active " << active;
#ifndef _NDEBUG
    // constexpr auto refresh_threshold = 110 * 60;
    constexpr auto refresh_threshold = 60;
#else
    constexpr auto refresh_threshold = 60;
#endif
    if (active && m_next_token_refresh_at > 0 && m_next_token_refresh_at - now < refresh_threshold) {
        BOOST_LOG_TRIVIAL(info) << "Enqueue access token refresh on activation";
        m_token_timer->Stop();
        enqueue_refresh();
    }
}

void UserAccountCommunication::wakeup_session_thread()
{
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
    const auto prior_expiration_secs = 5 * 60;
    int milliseconds = std::max((seconds - prior_expiration_secs) * 1000, 60000);
    m_next_token_refresh_at = std::time(nullptr) + milliseconds / 1000;
    m_token_timer->StartOnce(milliseconds);
}


void UserAccountCommunication::on_token_timer(wxTimerEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << "UserAccountCommunication: Token refresh timer fired";
    enqueue_refresh();
}
void UserAccountCommunication::on_polling_timer(wxTimerEvent& evt)
{
    if (!m_window_is_active) {
        return;
    }
    wakeup_session_thread();
}

std::string CodeChalengeGenerator::generate_chalenge(const std::string& verifier)
{
    std::string code_challenge;
    try
    {
        code_challenge = sha256(verifier);
        code_challenge = base64_encode(code_challenge);
    }
    catch (const std::exception& e)
    {
        BOOST_LOG_TRIVIAL(error) << "Code Chalenge Generator failed: " << e.what();
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
