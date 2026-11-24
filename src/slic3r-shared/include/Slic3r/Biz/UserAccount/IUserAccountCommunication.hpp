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

class IPairingCommunication;

class IUserAccountCommunication {
public:
    virtual ~IUserAccountCommunication() = default;

    /**
     * @brief Whether the account is a Prusa Account, whose token the other Prusa services
     * (Printables, the preset repository) accept. False for a Connect Local credential.
     */
    virtual bool is_prusa_account() const = 0;

    /**
     * @brief The app pairing side of a Connect Local communication, null for the others.
     */
    virtual IPairingCommunication* pairing() = 0;

    // From UserAccountCommunicationTokenBase
    virtual void init() = 0;
    virtual bool is_active() const = 0;
    virtual std::string
    wait_for_access_token(std::chrono::milliseconds timeout, JThread::StopToken stop_token) = 0;

    virtual void set_refresh_time(int seconds) = 0;

    virtual void
    on_username_changed(const std::string& username, const std::string& id, bool store) = 0;

    virtual void on_race_lost(const std::string& body) = 0;
    virtual void add_session_listener(IUserAccountSessionListener* listener) = 0;
    virtual void on_read_token_store_message() = 0;
    virtual void request_refresh() = 0;
    virtual bool validate_and_refresh() = 0;
    virtual std::string username() const = 0;
    virtual std::string user_id() const = 0;
    virtual void cancel_ongoing_session_action() = 0;

    // From UserAccountCommunication
    /**
     * @brief Logs out: when the person asks for it and before the communication is replaced
     * (another service or server is chosen, the account is turned off). Nothing of the account
     * stays stored. With Connect Local the credential is also revoked on the server.
     */
    virtual void do_log_out(bool notify_owner) = 0;

    virtual std::string on_log_in_request(
        const std::string& lang_code,
        bool generate_code_verifier,
        const std::string& service
    ) = 0;

    virtual void on_log_in_code_response(const std::string& url_message) = 0;
    virtual bool is_logged_in() const = 0;
    virtual std::string access_token() const = 0;
    virtual boost::filesystem::path avatar() const = 0;
    virtual void on_avatar_url(const std::string& data) = 0;
    virtual void on_avatar_success(std::string&& data) const = 0;
    virtual std::string email() const = 0;
    virtual void on_email(const std::string& data) = 0;
    virtual void request_printables_secret_token() = 0;
    virtual void enqueue_connect_printers_data_action(std::function<void(const std::string&)> succ_fn) = 0;

    /**
     * @brief The server refused access_token (HTTP 401) somewhere outside the session, e.g. on the
     * printer list or an upload. Ignored unless the token is still the current one; then a Prusa
     * Account refreshes it if it has expired, a Connect Local credential cannot be renewed and
     * logs out.
     */
    virtual void on_unauthorized(const std::string& access_token) = 0;

    /**
     * @brief The account reply (/me) has no usable public username.
     */
    virtual void on_user_id_unusable() = 0;
};

} // namespace Slic3r::Biz::UserAccount