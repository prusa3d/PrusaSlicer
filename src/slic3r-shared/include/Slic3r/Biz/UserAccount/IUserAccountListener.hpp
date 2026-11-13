#pragma once

#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"

#include <ctime>
#include <string>
#include <functional>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Interface for events going outside whole UserAccount logic.
 */
class IUserAccountListener
{
public:
    virtual ~IUserAccountListener() = default;
    virtual void on_user_account_id_success(bool is_refresh, const std::string& username) {};
    virtual void on_avatar_downloaded() {};
    virtual void on_user_account_logged_out() {};
    virtual void on_user_account_logged_out_notify_instances() {};
    virtual void on_user_account_will_refresh() {};
    virtual void on_user_account_action_retry(
        const Network::IHttp::Retry& retry,
        std::function<void(void)> cancel_callback
    ) {};
    /**
     * @brief Called when a retried session action reached any terminal state - success, fail,
     * lost token refresh race or log out. No retry of that action is in flight anymore.
     */
    virtual void on_user_account_action_retry_finished() {};
    virtual void on_printables_secret_token(const std::string& body) {};

    virtual void on_user_account_enabled_state_changed(bool is_enabled) {};

    /**
     * @brief Connect Local app pairing started (or a waiting one is shown again): the person
     * approves user_code on page_url.
     */
    virtual void
    on_user_account_pairing_ready(const std::string& page_url, const std::string& user_code) {};
    /**
     * @brief Connect Local app pairing ended without signing in.
     */
    virtual void on_user_account_pairing_failed(PairingFailure failure) {};
    /**
     * @brief Connect Local app pairing approved and its credential collected. Comes before the
     * account is loaded and on_user_account_id_success is announced.
     */
    virtual void on_user_account_pairing_approved() {};
    /**
     * @brief The Connect Local credential expires soon (unix time) and cannot be renewed: the
     * person has to sign in again after it.
     */
    virtual void on_user_account_credential_expires_soon(std::time_t expires_at) {};
};

class IConnectHandlerListener 
{
public:
    virtual void on_select_printer_from_connect(const std::string& printer_json) = 0;
};
} // namespace Slic3r::Biz::UserAccount
