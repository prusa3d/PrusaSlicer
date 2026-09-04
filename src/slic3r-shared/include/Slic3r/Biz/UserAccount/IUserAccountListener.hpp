#pragma once

#include "Slic3r/Biz/Network/IHttp.hpp"
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
};

class IConnectHandlerListener 
{
public:
    virtual void on_select_printer_from_connect(const std::string& printer_json) = 0;
};
} // namespace Slic3r::Biz::UserAccount
