#pragma once

#include "Slic3r/Biz/UserAccount/UserAccountActionStore.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountSessionDispatchBase.hpp"

#include <queue>
#include <mutex>
#include <deque>
#include <atomic>
#include <cstdint>

namespace Slic3r::Biz::UserAccount {
/**
 * @brief Queue of UserAccountAction in own thread.
 * Receives and queues requests of Actions.
 * Performs Actions including handling its results (f.e. resolving tokens)
 * Passes data outside via IMainThreadDispatcher implemented in UserAccountSessionDispatchBase.
 */
class UserAccountSession final : public UserAccountSessionDispatchBase
{
public:
    UserAccountSession(Platform::IMainThreadDispatcher& dispatcher);
    ~UserAccountSession() = default;

    UserAccountSession(const UserAccountSession&)             = delete;
    UserAccountSession(UserAccountSession&& other)            = delete;
    UserAccountSession& operator=(const UserAccountSession&)  = delete;
    UserAccountSession& operator=(UserAccountSession&& other) = delete;

    void set_tokens(const std::string& access_token, const std::string& refresh_token, const std::string& shared_session_key, long long expires_in);

    /**
     * @brief Clears all data, logs out.
     */
    void do_clear(bool notify_owner);

    /**
     * @brief Permanently stops the session. Unlike cancel_ongoing_session_action, every further
     * enqueue, queue processing and action result becomes a no-op.
     */
    void stop();

    /**
     * @brief One by one processes whole action queue.
     */
    void process_action_queue();

    /**
     * @brief Enqueues CodeForToken action with callbacks.
     */
    void on_log_in_code_response(const std::string& code, const std::string& code_verifier);
    void enqueue_action(ActionQueueData&& action);
    void enqueue_test_with_refresh();
    void enqueue_refresh(const std::string& body);
    void enqueue_refresh_race(const std::string& refresh_token_from_store);

    bool is_enqueued(UserAccountActionID action_id) const;

    bool is_initialized() const
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return !m_access_token.empty() || !m_refresh_token.empty();
    }

    std::string get_access_token() const
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_access_token;
    }

    std::string get_refresh_token() const
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_refresh_token;
    }

    std::string get_shared_session_key() const
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_shared_session_key;
    }

    long long get_next_token_timeout() const
    {
        std::lock_guard<std::mutex> lock(m_credentials_mutex);
        return m_next_token_timeout;
    }

    void cancel_ongoing_session_action()
    {
        m_global_cancel = true;
    }

    /**
     * @brief Tells the session whether the owner holds a username, so that a logout can report
     * whether it ended a session the user could see.
     */
    void set_has_username(bool has_username)
    {
        m_has_username = has_username;
    }

private:
    mutable std::mutex m_session_mutex;
    // guarded by m_session_mutex

    UserAccountActionStore m_actions;
    std::queue<ActionQueueData> m_action_queue;
    std::deque<ActionQueueData> m_priority_action_queue;
    /**
     * @brief Prevents action queue to be processed if false - no communication is done
     *  sets to true by on_log_in_code_response or enqueue_action call
     */
    bool m_processing_enabled{false};

    void enqueue_action_inner(ActionQueueData&& action);

    // End of section guarded by m_session_mutex

    mutable std::mutex m_credentials_mutex;
    // guarded by m_credentials_mutex
    std::string m_access_token;
    std::string m_refresh_token;
    std::string m_shared_session_key;
    long long m_next_token_timeout{0};
    // End of section guarded by m_credentials_mutex

    std::atomic_bool m_global_cancel{false};

    std::atomic_bool m_shutting_down{false};
    std::atomic_bool m_has_username{false};

    /**
     * @brief Bumped on every logout (do_clear). Token-producing requests capture the
     * epoch when enqueued; both their success and failure handlers discard the result if
     * the epoch has since changed, so a request that was already in flight when the user
     * logged out (or when another instance's token was adopted from the store) cannot
     * write tokens / log the instance back in - and, just as importantly, its late failure
     * cannot tear down a newer session that has meanwhile been established.
     */
    std::atomic<uint64_t> m_session_epoch{0};
    /**
     * Called to pass data from Session thread to UI thread.
     */

    void refresh_fail_callback(const std::string& body, uint64_t epoch);
    void refresh_fail_soft_callback(const std::string& body, uint64_t epoch);
    void cancel_queue();
    void code_exchange_fail_callback(const std::string& body, uint64_t epoch);
    void token_success_callback(const std::string& body, uint64_t epoch);
    void process_action_queue_inner();
    void remove_from_queue(UserAccountActionID action_id);
};
} // namespace Slic3r::Biz::UserAccount
