#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountCommunication.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountSession.hpp"

#include <jthread/JThread.hpp>
#include <mutex>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Manages token refresh with respect to multiple instances using same token.
 */
class UserAccountCommunicationTokenBase : public IUserAccountCommunication
{
public:
    UserAccountCommunicationTokenBase(Platform::IMainThreadDispatcher& dispatcher);
    ~UserAccountCommunicationTokenBase();

    UserAccountCommunicationTokenBase(const UserAccountCommunicationTokenBase&)            = delete;
    UserAccountCommunicationTokenBase(UserAccountCommunicationTokenBase&& other)           = delete;
    UserAccountCommunicationTokenBase& operator=(const UserAccountCommunicationTokenBase&) = delete;
    UserAccountCommunicationTokenBase& operator=(UserAccountCommunicationTokenBase&& other) = delete;

    void init() override;

    bool is_active() const override { return true; }

    /**
     *@brief Sets token timer to try refreshing tokens based on given input.
     * Latest refresh is 10 seconds before token expires.
     */
    void set_refresh_time(int seconds) override;

    /**
     * @brief Writes tokens to token store, if store is true or username empty (which is on log out).
     */
    void on_username_changed(const std::string& username, bool store) override;

    /**
     * @brief Called after enqueue_refresh_race on session failed.
     */
    void on_race_lost(const std::string& body) override;

    void add_session_listener(IUserAccountSessionListener* listener) override
    {
        m_session.add_listener<IUserAccountSessionListener>(listener);
    }

    /**
     * @brief Called when other instance changed tokens in store.
     */
    void on_read_token_store_message() override;

    /**
     * @brief This function is called when Printables requests new token - same token as we have now wont do.
     */
    void request_refresh() override;

    /**
     * @brief Starts refresh sequence of access token if needed. Returns true if refresh started.
     * Called from Connect webpage wrapper to prevent posting expired token.
     */
    bool validate_and_refresh() override;

    std::string username() const override
    {
        return m_username;
    }

    void cancel_ongoing_session_action()
    {
        m_session.cancel_ongoing_session_action();
    }

protected:
    UserAccountSession m_session;

    /**
     * @brief Logs out. All tokens are thrown out and deleted from store. All timers are stopped.
     */
    void do_clear(bool notify_owner);

    /**
     * @brief Wakes up m_thread. Needs to be call if Session thread should start processing its action queue.
     */
    void wakeup_session_thread();

private:
    void on_token_timer();
    void on_slave_read_timer();
    void on_after_race_lost_timer();

    void set_tokens(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, const std::string& next_token_timeout);
    void set_tokens_to_session(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, long long next_token_timeout);
    void stop_all_timers();
    void stop_token_timers();

    /**
     * @brief Implementation of thread function.
     */
    void init_session_thread();

    void enqueue_refresh();
    void enqueue_refresh_race(const std::string& refresh_token);

    std::string m_username;

    Platform::TimerQueue::TimerID m_token_timer_id;
    Platform::TimerQueue::TimerID m_slave_read_timer_id;
    Platform::TimerQueue::TimerID m_after_race_lost_timer_id;

    int m_last_token_duration_seconds{0};
    std::time_t m_next_token_refresh_at{0};

    std::mutex m_thread_stop_mutex;
    std::condition_variable m_thread_stop_condition;
    bool m_thread_wakeup{true};
    bool m_window_is_active{true};

    std::string refresh_token_from_store;

    JThread::JThread m_thread;
};

} // namespace Slic3r::Biz::UserAccount
