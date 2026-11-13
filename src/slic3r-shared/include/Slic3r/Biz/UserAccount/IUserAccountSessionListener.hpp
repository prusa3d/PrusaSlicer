#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"

#include <ctime>
#include <string>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Interface for events going from Session thread to main thread.
 */
class IUserAccountSessionListener
{
public:
    virtual ~IUserAccountSessionListener() = default;

    virtual void on_action_retry(const Network::IHttp::Retry& retry)                 = 0;
    virtual void on_action_success(ActionSuccessType success_type, std::string body) = 0;
    virtual void on_action_fail(ActionFailType fail_type, std::string body)          = 0;
    virtual void on_enqueued_refresh()                                               = 0;
    virtual void on_new_refresh_time(long long exp)                                  = 0;
    virtual void on_race_lost(const std::string& body)                               = 0;
    virtual void on_logged_out(bool notify_owner, bool was_logged_in)                = 0;
    virtual void on_printables_secret_token(const std::string& body)                 = 0;

    // Connect Local app pairing (offline sign-in), called on the main thread.
    virtual void on_pairing_ready(const std::string& page_url, const std::string& user_code) = 0;
    virtual void on_pairing_failed(PairingFailure failure)                                   = 0;
    /**
     * @brief The credential is collected; the account is loaded (and announced) only after this.
     */
    virtual void on_pairing_approved()                              = 0;
    virtual void on_credential_expires_soon(std::time_t expires_at) = 0;
};
} // namespace Slic3r::Biz::UserAccount
