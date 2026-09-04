#include "Slic3r/Biz/UserAccount/UserAccountCommunicationTokenBase.hpp"

#include "Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountTokenLog.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Network/Jwt.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include "Slic3r/Biz/AppInstance/AppInstanceUtils.hpp" // get_current_pid()

#include <exception>

namespace Slic3r::Biz::UserAccount {

namespace {
long long parse_next_timeout(const std::string& next_timeout)
{
    if (next_timeout.empty()) {
        return 0;
    }
    try {
        return std::stoll(next_timeout);
    } catch (const std::exception&) {
        SPDLOG_ERROR("Token store holds an unreadable expiration: {}", next_timeout);
        return 0;
    }
}
} // namespace

UserAccountCommunicationTokenBase::UserAccountCommunicationTokenBase(Platform::IMainThreadDispatcher& dispatcher) :
    m_session{dispatcher}
{
}

void UserAccountCommunicationTokenBase::init()
{
    TokenStore::StoreData stored_data;
    bool tokens_loaded = TokenStore::load_tokens(stored_data);

    if (!tokens_loaded) {
        SPDLOG_INFO("Token store is empty on startup.");
        init_session_thread();
        return;
    }

    long long next_timeout_long = parse_next_timeout(stored_data.next_timeout);

    if (stored_data.malformed || (!stored_data.refresh_token.empty() && next_timeout_long == 0)) {
        SPDLOG_WARN("Token store holds an unusable record - resetting it.");
        TokenStore::reset();
        init_session_thread();
        return;
    }

    long long remain_time = next_timeout_long - std::time(nullptr);
    if (remain_time <= 0) {
        stored_data.access_token.clear();
    } else {
        set_refresh_time((int) remain_time);
    }
    if (stored_data.access_token.empty()) {
        SPDLOG_WARN("{} access_token empty!", __func__);
    } else {
        SPDLOG_INFO("{} access_token: {}", __func__, token_log_fingerprint(stored_data.access_token));
    }

    bool has_token = !stored_data.refresh_token.empty();
    set_tokens_to_session(stored_data.access_token, stored_data.refresh_token, stored_data.shared_session_key, next_timeout_long);
    init_session_thread();
    // perform login at the start, but only with tokens
    if (has_token) {
        m_session.enqueue_test_with_refresh();
        wakeup_session_thread();
    }
}

UserAccountCommunicationTokenBase::~UserAccountCommunicationTokenBase()
{
    stop_all_timers();
    m_session.stop();

    // Since there is m_thread_stop_condition in m_thread,
    // we need to manually request_stop and wake up the condition for the thread to stop.
    m_thread.request_stop();
    wakeup_session_thread();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UserAccountCommunicationTokenBase::set_refresh_time(int seconds)
{
    auto& timer_queue = Platform::PlatformServices::instance().timer_queue();
    if (timer_queue.is_timer_running(m_token_timer_id)) {
        timer_queue.cancel_timer(m_token_timer_id);
    }
    m_last_token_duration_seconds    = seconds;
    const auto prior_expiration_secs = std::max(seconds / 24, 10);
    int milliseconds                 = std::max((seconds - prior_expiration_secs) * 1'000, 1'000);
    SPDLOG_INFO("Starting token timer. Next refresh in {} seconds.", milliseconds / 1'000);
    m_next_token_refresh_at          = std::time(nullptr) + milliseconds / 1'000;
    m_token_timer_id = timer_queue.set_timer(std::chrono::milliseconds(milliseconds), std::bind(&UserAccountCommunicationTokenBase::on_token_timer, this), false);
}

void UserAccountCommunicationTokenBase::on_token_timer()
{
    SPDLOG_INFO("{} T1", __FUNCTION__);

    // Read PID from current stored token, compare with own PID and decide if master / slave
    std::string my_pid = std::to_string(AppInstance::get_current_pid());

    TokenStore::StoreData stored_data;
    bool tokens_loaded = TokenStore::load_tokens(stored_data);
    if (!tokens_loaded || stored_data.refresh_token.empty()) {
        SPDLOG_INFO("Store is empty - logging out.");
        do_clear(true);
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : parse_next_timeout(stored_data.next_timeout) - std::time(nullptr);
    if (my_pid == stored_data.master_pid) {
        enqueue_refresh();
        return;
    }
    // token could be either already new -> we want to start using it now
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second >= 0 && expires_in_second > prior_expiration_secs) {
        SPDLOG_INFO("Current token has different PID - expiration is {} while longest expected was {}. Using this token.", expires_in_second, prior_expiration_secs);
        set_tokens(
            stored_data.access_token,
            stored_data.refresh_token,
            stored_data.shared_session_key,
            stored_data.next_timeout
        );
        return;
    }
    // or yet to be renewed -> we should wait to give time to master to renew it
    if (expires_in_second >= 0) {
        SPDLOG_INFO("Current token has different PID - waiting {}", expires_in_second / 2);
        m_slave_read_timer_id = Platform::PlatformServices::instance().timer_queue().set_timer(
            std::chrono::milliseconds((expires_in_second / 2) * 1'000),
            std::bind(&UserAccountCommunicationTokenBase::on_slave_read_timer, this),
            false
        );
        return;
    }
    // or expired -> renew now.
    SPDLOG_INFO("Current token has different PID and is expired.");
    enqueue_refresh_race(stored_data.refresh_token);
}

void UserAccountCommunicationTokenBase::on_slave_read_timer()
{
    SPDLOG_INFO("{} T2", __FUNCTION__);
    std::string current_access_token = m_session.get_access_token();

    TokenStore::StoreData stored_data;
    bool tokens_loaded = TokenStore::load_tokens(stored_data);
    if (!tokens_loaded || stored_data.refresh_token.empty()) {
        SPDLOG_INFO("Store is empty - logging out.");
        do_clear(true);
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : parse_next_timeout(stored_data.next_timeout) - std::time(nullptr);
    if (stored_data.access_token != current_access_token) {
        // consider stored_data as renewed token from master
        SPDLOG_INFO("Token in store seems to be new - using it.");
        set_tokens(
            stored_data.access_token,
            stored_data.refresh_token,
            stored_data.shared_session_key,
            stored_data.next_timeout
        );
        return;
    }
    if (stored_data.access_token != current_access_token) {
        // token is expired
        SPDLOG_INFO("Token in store seems to be new but expired - refreshing now.");
        enqueue_refresh_race(stored_data.refresh_token);
        return;
    }
    SPDLOG_INFO("No new token, enqueueing refresh (race expected).");
    enqueue_refresh_race({});
}

void UserAccountCommunicationTokenBase::
    set_tokens(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, const std::string& next_token_timeout)
{
    stop_token_timers();
    long long next = parse_next_timeout(next_token_timeout);
    set_tokens_to_session(access_token, refresh_token, shared_session_key, next);
}

void UserAccountCommunicationTokenBase::set_tokens_to_session(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, long long next_token_timeout)
{
    m_session.set_tokens(access_token, refresh_token, shared_session_key, next_token_timeout);
}

void UserAccountCommunicationTokenBase::stop_token_timers()
{
    auto& timer_queue = Platform::PlatformServices::instance().timer_queue();
    if (timer_queue.is_timer_running(m_token_timer_id)) {
        timer_queue.cancel_timer(m_token_timer_id);
    }
    if (timer_queue.is_timer_running(m_slave_read_timer_id)) {
        timer_queue.cancel_timer(m_slave_read_timer_id);
    }
    if (timer_queue.is_timer_running(m_after_race_lost_timer_id)) {
        timer_queue.cancel_timer(m_after_race_lost_timer_id);
    }
}

void UserAccountCommunicationTokenBase::stop_all_timers()
{
    auto& timer_queue = Platform::PlatformServices::instance().timer_queue();
    if (timer_queue.is_timer_running(m_token_timer_id)) {
        timer_queue.cancel_timer(m_token_timer_id);
    }
    if (timer_queue.is_timer_running(m_slave_read_timer_id)) {
        timer_queue.cancel_timer(m_slave_read_timer_id);
    }
    if (timer_queue.is_timer_running(m_after_race_lost_timer_id)) {
        timer_queue.cancel_timer(m_after_race_lost_timer_id);
    }
}

void UserAccountCommunicationTokenBase::init_session_thread()
{
    m_thread = JThread::JThread(
        [&](JThread::StopToken stop_token)
        {
            for (;;) {
                {
                    std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
                    m_thread_stop_condition.wait_for(
                        lck,
                        std::chrono::seconds(88'888),
                        [this, stop_token] { return stop_token.stop_requested() || m_thread_wakeup; }
                    );
                }
                if (stop_token.stop_requested())
                    // Stop the worker thread.
                    break;
                // Do not process_action_queue if window is not active and thread was not forced to wakeup
                if (!m_window_is_active && !m_thread_wakeup) {
                    continue;
                }
                m_thread_wakeup = false;
                m_session.process_action_queue();
            }
        }
    );
}

void UserAccountCommunicationTokenBase::do_clear(bool notify_owner)
{
    m_session.do_clear(notify_owner);
    on_username_changed({}, true);
    stop_all_timers();
    m_next_token_refresh_at       = 0;
    m_last_token_duration_seconds = 0;
    refresh_token_from_store.clear();
}

void UserAccountCommunicationTokenBase::on_username_changed(const std::string& username, bool store)
{
    m_username = username;
    m_session.set_has_username(!username.empty());
    if (!store && !username.empty()) {
        return;
    }

    SPDLOG_INFO(
        "{} access_token: {}",
        __FUNCTION__,
        username.empty() ? std::string("[logged out]") : token_log_fingerprint(m_session.get_access_token())
    );

    TokenStore::StoreData stored_data{
        m_session.get_access_token(),
        m_session.get_refresh_token(),
        m_session.get_shared_session_key(),
        username.empty() ? std::string() : std::to_string(m_session.get_next_token_timeout()),
        username.empty() ? std::string() : std::to_string(AppInstance::get_current_pid())
    };
    if (!TokenStore::save_tokens(stored_data)) {
        SPDLOG_INFO("Failed to write tokens to the secret store.");
    }
}

void UserAccountCommunicationTokenBase::enqueue_refresh()
{
    if (!m_session.is_initialized()) {
        SPDLOG_ERROR("Connect Printers endpoint connection failed - Not Logged in.");
        return;
    }
    if (m_session.is_enqueued(UserAccountActionID::RefreshToken)) {
        SPDLOG_INFO("User Account: Token refresh already enqueued, skipping...");
        return;
    }
    m_session.enqueue_refresh({});
    wakeup_session_thread();
}

void UserAccountCommunicationTokenBase::wakeup_session_thread()
{
    {
        std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
        m_thread_wakeup = true;
    }
    m_thread_stop_condition.notify_all();
}

void UserAccountCommunicationTokenBase::enqueue_refresh_race(const std::string& refresh_token)
{
    SPDLOG_INFO("{} T3", __FUNCTION__);
    if (!m_session.is_initialized()) {
        SPDLOG_ERROR("Connect Printers endpoint connection failed - Not Logged in.");
        return;
    }
    if (refresh_token_from_store.empty() && m_session.is_enqueued(UserAccountActionID::RefreshToken))
    {
        SPDLOG_ERROR("{} Token refresh already enqueued, skipping...", __FUNCTION__);
        return;
    }
    // At this point, last master did not renew the tokens, behave like master
    m_session.enqueue_refresh_race(refresh_token);
    wakeup_session_thread();
}

void UserAccountCommunicationTokenBase::on_race_lost(const std::string& body)
{
    SPDLOG_INFO("{} T4", __FUNCTION__);
    // race from on_slave_read_timer has been lost
    // other instance was faster to renew tokens so refresh token from this app was denied (invalid grant)
    // we should read the other token now.
    std::string current_access_token = m_session.get_access_token();

    TokenStore::StoreData stored_data;
    bool tokens_loaded = TokenStore::load_tokens(stored_data);
    if (!tokens_loaded || stored_data.refresh_token.empty()) {
        SPDLOG_INFO("Store is empty - logging out.");
        do_clear(true);
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : parse_next_timeout(stored_data.next_timeout) - std::time(nullptr);
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second > 0 && expires_in_second > prior_expiration_secs) {
        SPDLOG_INFO("Token is alive - using it.");
        set_tokens(
            stored_data.access_token,
            stored_data.refresh_token,
            stored_data.shared_session_key,
            stored_data.next_timeout
        );
        return;
    }

    SPDLOG_INFO("No suitable token found waiting ", std::max((expires_in_second / 2), (long long) 2));
    int miliseconds_to_timer   = std::max((expires_in_second / 2) * 1'000, (long long) 2'000);
    m_after_race_lost_timer_id = Platform::PlatformServices::instance().timer_queue().set_timer(
        std::chrono::milliseconds(miliseconds_to_timer),
        std::bind(&UserAccountCommunicationTokenBase::on_after_race_lost_timer, this),
        false
    );
}

void UserAccountCommunicationTokenBase::on_after_race_lost_timer()
{
    SPDLOG_INFO("{} T5", __FUNCTION__);

    std::string current_access_token = m_session.get_access_token();

    TokenStore::StoreData stored_data;
    bool tokens_loaded = TokenStore::load_tokens(stored_data);
    if (!tokens_loaded || stored_data.refresh_token.empty()) {
        SPDLOG_INFO("Store is empty - logging out.");
        do_clear(true);
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : parse_next_timeout(stored_data.next_timeout) - std::time(nullptr);
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second > 0 && expires_in_second > prior_expiration_secs) {
        SPDLOG_INFO("Token is alive - using it.");
        set_tokens(
            stored_data.access_token,
            stored_data.refresh_token,
            stored_data.shared_session_key,
            stored_data.next_timeout
        );
        return;
    }
    SPDLOG_INFO("No new token is stored - This is error state. Logging out.");
    do_clear(true);
}

void UserAccountCommunicationTokenBase::on_read_token_store_message()
{
    SPDLOG_INFO(__FUNCTION__);
    TokenStore::StoreData stored_data;
    bool tokens_loaded = TokenStore::load_tokens(stored_data);
    if (!tokens_loaded || stored_data.refresh_token.empty()) {
        if (!username().empty() || m_session.is_initialized()) {
            SPDLOG_INFO("Store is empty - logging out.");
            do_clear(false); // The false here is important, we do not want to notify other instances - we were just notified.
        }
        return;
    }

    std::string current_access_token = m_session.get_access_token();
    if (current_access_token == stored_data.access_token) {
        SPDLOG_INFO("Current token is up to date.");
        return;
    }

    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : parse_next_timeout(stored_data.next_timeout) - std::time(nullptr);
    const auto prior_expiration_secs = std::max(m_last_token_duration_seconds / 24, 10);
    if (expires_in_second > 0) {
        SPDLOG_INFO("Token is alive - using it.");
        const bool was_logged_out = username().empty();
        set_tokens(
            stored_data.access_token,
            stored_data.refresh_token,
            stored_data.shared_session_key,
            stored_data.next_timeout
        );
        if (was_logged_out) {
            m_session.enqueue_test_with_refresh();
            wakeup_session_thread();
        }
        return;
    }
}

void UserAccountCommunicationTokenBase::request_refresh()
{
    stop_token_timers();

    TokenStore::StoreData stored_data;
    bool tokens_loaded = TokenStore::load_tokens(stored_data);
    if (!tokens_loaded || stored_data.refresh_token.empty()) {
        SPDLOG_INFO("Store is empty - logging out.");
        do_clear(false);
        return;
    }
    std::string current_access_token = m_session.get_access_token();

    // Here we need to count with situation when token was renewed in m_session but was not yet stored.
    // Then store token is not valid - it should has earlier expiration
    long long expires_in_second = stored_data.next_timeout.empty() ? 0 : parse_next_timeout(stored_data.next_timeout) - std::time(nullptr);
    if (stored_data.access_token != current_access_token && expires_in_second > 0 && expires_in_second > m_next_token_refresh_at - std::time(nullptr))
    {
        SPDLOG_INFO("Found usable token. Expires in {}", expires_in_second);
        set_tokens(
            stored_data.access_token,
            stored_data.refresh_token,
            stored_data.shared_session_key,
            stored_data.next_timeout
        );
    } else {
        enqueue_refresh_race(stored_data.refresh_token);
    }
}

bool UserAccountCommunicationTokenBase::validate_and_refresh()
{
    std::string current_access_token = m_session.get_access_token();
    if (current_access_token.empty())
    {
        SPDLOG_WARN("Queried token was empty.");
        return false;
    }

    if (Network::Jwt::verify_exp(current_access_token))
    {
        return false;
    }

    request_refresh();
    return true;
}

} // namespace Slic3r::Biz::UserAccount
