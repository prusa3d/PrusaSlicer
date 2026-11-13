#pragma once

#include "Slic3r/Biz/UserAccount/UserAccountCommunicationBase.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountPairing.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountTokenStore.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::UserAccount {

class BackgroundWorker;

/**
 * @brief Offline (Connect Local) sign-in by app pairing and a bearer credential.
 *
 * The person approves the pairing on the server's /pair page, the app polls until it gets a
 * credential (an access_token valid for 90 days, without refresh). The credential is the bearer
 * of every request and is kept in the offline token store together with the server it belongs to.
 * When the server refuses it (401) or it expires, the app switches to another valid credential
 * in the store or logs out; a logout the person asks for revokes it on the server.
 *
 * UserAccountSession serves only as the request queue and the holder of the token; none of its
 * OAuth refresh or code exchange paths is used here.
 */
class UserAccountCommunicationPairing final : public UserAccountCommunicationBase
{
public:
    explicit UserAccountCommunicationPairing(Platform::IMainThreadDispatcher& dispatcher);
    ~UserAccountCommunicationPairing() override;

    void init() override;
    void add_session_listener(IUserAccountSessionListener* listener) override;
    void
    on_username_changed(const std::string& username, const std::string& id, bool store) override;
    void on_user_id_unusable() override;
    void on_read_token_store_message() override;
    /**
     * @brief There is nothing to refresh: checks the credential with the server instead (at most
     * once per 30 s, never twice at once) and logs out if it is refused.
     */
    void request_refresh() override;
    /**
     * @brief Drops the credential when it has expired and returns true then, so that no expired
     * token is handed out.
     */
    bool validate_and_refresh() override;

    /**
     * @brief Revokes the credential on the server (best effort) and empties the store.
     */
    void do_log_out(bool notify_owner) override;
    void log_out_locally(bool notify_owner) override;
    /**
     * @brief Printables would not accept a Connect Local token: answers with none.
     */
    void request_printables_secret_token() override;
    void
    enqueue_connect_printers_data_action(std::function<void(const std::string&)> succ_fn) override;
    void enqueue_page_session_action(
        std::function<void(std::optional<std::string>)> done,
        bool fresh
    ) override;

    void start_pairing() override;
    void cancel_pairing() override;
    void on_unauthorized(const std::string& access_token) override;
    /**
     * @brief Connect Local cannot know its own address and answers avatar_small relative to the
     * /me URL: resolves it against that one (an absolute URL is kept) before the download.
     */
    void on_avatar_url(const std::string& data) override;

private:
    /** @brief A page session handed out, see enqueue_page_session_action(). */
    struct PageSession
    {
        std::string token;
        std::string reply;
        std::chrono::steady_clock::time_point opened;
    };

    /** @brief Runs fn on the main thread unless this object is gone by then. */
    void post(std::function<void()> fn);
    void cancel_timer(Platform::TimerQueue::TimerID& id);

    /** @brief Uses a credential and checks it with the server (/me). */
    void adopt(const TokenStore::OfflineCredential& credential);
    void set_credential(const TokenStore::OfflineCredential& credential);
    void enqueue_verification();
    void on_verified(const std::string& token);
    void on_verification_failed(const std::string& token, unsigned status);
    /**
     * @brief The server could not confirm the account of `token`: asks again later, backing off.
     */
    void retry_verification(const std::string& token);
    bool expired(const TokenStore::OfflineCredential& credential) const;
    /**
     * @brief A valid credential of this server in the store other than `token`, if the store can
     * tell.
     */
    std::optional<TokenStore::OfflineCredential>
    other_stored_credential(const std::string& token) const;
    /**
     * @brief The server refused `token` (401) or it expired: switch to another valid credential
     * of the store, or log out. Nothing is revoked, the credential is useless anyway.
     */
    void on_credential_unusable(const std::string& token);
    /**
     * @brief Drops an expired credential (returns true then) and warns of the expiration a week
     * before. Run hourly while a credential is held: a wall clock check, a long timer would
     * drift over sleep.
     */
    bool check_expiration();

    void on_pairing_started(uint64_t pairing_id, const std::string& body);
    void on_pairing_start_failed(uint64_t pairing_id, unsigned status, const std::string& body);
    void schedule_poll(int seconds);
    void poll(uint64_t pairing_id);
    void on_poll_reply(uint64_t pairing_id, unsigned status, const std::string& body);
    void on_credential(const Pairing::Credential& credential);
    void notify_pairing_failed(PairingFailure failure);

    /** @brief Revokes a credential on the server, in the background, ignoring the result. */
    void revoke(const std::string& credential_id, const std::string& token);
    /**
     * @brief Logs out locally (session, timers, user data).
     * @param revoke_credential Also revoke the credential on the server.
     * @param clear_store Empty the store if it holds this credential.
     */
    void log_out(bool notify_owner, bool revoke_credential, bool clear_store);

    Platform::IMainThreadDispatcher& m_dispatcher;
    std::vector<IUserAccountSessionListener*> m_listeners;
    std::unique_ptr<BackgroundWorker> m_revocation_worker;

    /** @brief The credential in use, token empty when none. */
    TokenStore::OfflineCredential m_credential;
    /**
     * @brief A freshly paired credential whose account is not loaded yet: the dialog waits for it.
     */
    bool m_awaiting_first_login{false};
    bool m_account_unavailable_reported{false};
    bool m_expiration_warned{false};
    bool m_verification_in_flight{false};
    std::chrono::steady_clock::time_point m_last_requested_verification{};
    std::chrono::seconds m_verification_retry_delay;
    Platform::TimerQueue::TimerID m_expiration_timer;
    Platform::TimerQueue::TimerID m_verification_retry_timer;
    std::optional<PageSession> m_page_session;

    // Pairing, main thread only. One step runs at a time: the start request, a poll timer or a
    // poll request.
    Pairing::Poller m_poller;
    /** @brief Bumped by every start and cancel, replies of an older pairing are ignored. */
    uint64_t m_pairing_id{0};
    Platform::TimerQueue::TimerID m_poll_timer;

    /** @brief Expires with this object, so that late callbacks are dropped. */
    std::shared_ptr<char> m_alive{std::make_shared<char>(0)};
    /**
     * @brief Never reassigned, safe to copy from the session thread while m_alive is being reset.
     */
    const std::weak_ptr<char> m_alive_weak{m_alive};
};

} // namespace Slic3r::Biz::UserAccount
