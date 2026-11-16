#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"

#include <ctime>
#include <functional>
#include <optional>
#include <string>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Events of the Connect Local app pairing (Connect Local sign-in), called on the main thread.
 */
class IPairingListener
{
public:
    virtual ~IPairingListener() = default;

    virtual void on_pairing_ready(const std::string& page_url, const std::string& user_code) = 0;
    virtual void on_pairing_failed(PairingFailure failure)                                   = 0;
    /**
     * @brief The credential is collected; the account is loaded (and announced) only after this.
     */
    virtual void on_pairing_approved()                              = 0;
    virtual void on_credential_expires_soon(std::time_t expires_at) = 0;
};

/**
 * @brief What only a communication that signs in by app pairing (Connect Local) offers.
 * Obtained through IUserAccountCommunication::pairing(), null for the other communications.
 */
class IPairingCommunication
{
public:
    virtual ~IPairingCommunication() = default;

    virtual void set_pairing_listener(IPairingListener* listener) = 0;

    /**
     * @brief Starts an app pairing, replacing a running one. The result comes to the pairing
     * listener (on_pairing_ready / on_pairing_failed, then the usual user id success).
     */
    virtual void start_pairing() = 0;

    /**
     * @brief Stops a running pairing, its result is ignored.
     */
    virtual void cancel_pairing() = 0;

    /**
     * @brief Gets a page session for the Connect Local web pages, the credential is the bearer.
     * done gets the reply on the main thread, nullopt when the server gave no session. A refused
     * credential (401) logs out and done is not called; if another credential takes its place,
     * the session is asked for again with that one.
     * @param fresh Open a new session even if a recent one could be handed out again (e.g. the
     * page reported that its session is not accepted anymore).
     */
    virtual void
    enqueue_page_session_action(std::function<void(std::optional<std::string>)> done, bool fresh) = 0;
};

} // namespace Slic3r::Biz::UserAccount
