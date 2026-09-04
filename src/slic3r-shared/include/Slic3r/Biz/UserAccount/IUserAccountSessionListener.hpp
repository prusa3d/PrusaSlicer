#pragma once

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
};
} // namespace Slic3r::Biz::UserAccount
