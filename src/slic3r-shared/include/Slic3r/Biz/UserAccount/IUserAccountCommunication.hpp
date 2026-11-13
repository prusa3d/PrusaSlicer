#pragma once

#include <chrono>
#include <string>
#include <functional>
#include <optional>
#include <boost/filesystem/path.hpp>
#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountSessionListener.hpp"

#include <jthread/JThread.hpp>

namespace Slic3r::Biz::UserAccount {

class IUserAccountCommunication {
public:
    virtual ~IUserAccountCommunication() = default;

    // From UserAccountCommunicationTokenBase
    virtual void init() = 0;
    virtual bool is_active() const = 0;
    virtual std::string
    wait_for_access_token(std::chrono::milliseconds timeout, JThread::StopToken stop_token) = 0;

    virtual void set_refresh_time(int seconds) {}

    virtual void
    on_username_changed(const std::string& username, const std::string& id, bool store) = 0;

    virtual void on_race_lost(const std::string& body) {}
    virtual void add_session_listener(IUserAccountSessionListener* listener) = 0;
    virtual void on_read_token_store_message() = 0;
    virtual void request_refresh() = 0;
    virtual bool validate_and_refresh() = 0;
    virtual std::string username() const = 0;
    virtual std::string user_id() const = 0;
    virtual void cancel_ongoing_session_action() = 0;

    // From UserAccountCommunication
    /**
     * @brief Logout the person asked for. Offline the credential is also revoked on the server.
     */
    virtual void do_log_out(bool notify_owner) = 0;

    /**
     * @brief Ends the session because the communication is being replaced (service mode or server
     * changed, account turned off). Offline nothing is revoked and the credential stays stored:
     * it may serve again when the same server is chosen.
     */
    virtual void log_out_locally(bool notify_owner)
    {
        do_log_out(notify_owner);
    }

    virtual std::string on_log_in_request(
        const std::string& lang_code,
        bool generate_code_verifier,
        const std::string& service
    )
    {
        return {};
    }

    virtual void on_log_in_code_response(const std::string& url_message) {}
    virtual bool is_logged_in() const = 0;
    virtual std::string access_token() const = 0;
    virtual boost::filesystem::path avatar() const = 0;
    virtual void on_avatar_url(const std::string& data) = 0;
    virtual void on_avatar_success(std::string&& data) const = 0;
    virtual std::string email() const = 0;
    virtual void on_email(const std::string& data) = 0;
    virtual void request_printables_secret_token() = 0;
    virtual void enqueue_connect_printers_data_action(std::function<void(const std::string&)> succ_fn) = 0;

    // Connect Local (offline mode). No-ops where the account signs in by OAuth.
    /**
     * @brief Starts an app pairing, replacing a running one. The result comes to the session
     * listeners (on_pairing_ready / on_pairing_failed, then the usual user id success).
     */
    virtual void start_pairing() {}

    /**
     * @brief Stops a running pairing, its result is ignored.
     */
    virtual void cancel_pairing() {}

    /**
     * @brief The server refused access_token (HTTP 401) somewhere outside the session, e.g. on the
     * printer list or an upload. Logs out if the token is still the current one and no other
     * credential can take over.
     */
    virtual void on_unauthorized(const std::string& access_token) {}

    /**
     * @brief The account reply (/me) has no usable public username.
     */
    virtual void on_user_id_unusable() {}

    /**
     * @brief Gets a page session for the Connect Local web pages, the credential is the bearer.
     * done gets the reply on the main thread, nullopt when the server gave no session. A refused
     * credential (401) logs out instead and done is not called.
     * @param fresh Open a new session even if a recent one could be handed out again (e.g. the
     * page reported that its session is not accepted anymore).
     */
    virtual void
    enqueue_page_session_action(std::function<void(std::optional<std::string>)> done, bool fresh)
    {}
};

} // namespace Slic3r::Biz::UserAccount