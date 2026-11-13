#pragma once

#include "Slic3r/Biz/UserAccount/UserAccountCommunicationBase.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Manages token refresh with respect to multiple instances using same token.
 */
class UserAccountCommunicationTokenBase : public UserAccountCommunicationBase
{
public:
    UserAccountCommunicationTokenBase(Platform::IMainThreadDispatcher& dispatcher);
    ~UserAccountCommunicationTokenBase();

    UserAccountCommunicationTokenBase(const UserAccountCommunicationTokenBase&)            = delete;
    UserAccountCommunicationTokenBase(UserAccountCommunicationTokenBase&& other)           = delete;
    UserAccountCommunicationTokenBase& operator=(const UserAccountCommunicationTokenBase&) = delete;
    UserAccountCommunicationTokenBase& operator=(UserAccountCommunicationTokenBase&& other) = delete;

    void init() override;

    /**
     *@brief Sets token timer to try refreshing tokens based on given input.
     * Latest refresh is 10 seconds before token expires.
     */
    void set_refresh_time(int seconds) override;

    /**
     * @brief Writes tokens to token store, if store is true or username empty (which is on log out).
     */
    void on_username_changed(const std::string& username, const std::string& id, bool store) override;

    /**
     * @brief Called after enqueue_refresh_race on session failed.
     */
    void on_race_lost(const std::string& body) override;

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

protected:
    /**
     * @brief Logs out. All tokens are thrown out and deleted from store. All timers are stopped.
     */
    void do_clear(bool notify_owner);

private:
    void on_token_timer();
    void on_slave_read_timer();
    void on_after_race_lost_timer();

    void set_tokens(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, const std::string& next_token_timeout);
    void set_tokens_to_session(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, long long next_token_timeout);
    void stop_all_timers();
    void stop_token_timers();

    void enqueue_refresh();
    void enqueue_refresh_race(const std::string& refresh_token);

    Platform::TimerQueue::TimerID m_token_timer_id;
    Platform::TimerQueue::TimerID m_slave_read_timer_id;
    Platform::TimerQueue::TimerID m_after_race_lost_timer_id;

    int m_last_token_duration_seconds{0};
    std::time_t m_next_token_refresh_at{0};

    std::string refresh_token_from_store;
};

} // namespace Slic3r::Biz::UserAccount
