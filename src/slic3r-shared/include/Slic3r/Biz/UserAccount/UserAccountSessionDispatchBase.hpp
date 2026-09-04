#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountSessionListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include <memory>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Base class for dispatching user account session events.
 * Also accepts callbacks from the UserAccountAction and dispatches them to listeners.
 */
class UserAccountSessionDispatchBase : public IUserAccountActionCallbacks, public WithListeners<IUserAccountSessionListener>
{
public:
    UserAccountSessionDispatchBase(Platform::IMainThreadDispatcher& dispatcher);
    ~UserAccountSessionDispatchBase();

    void on_action_retry(const Network::IHttp::Retry& retry) override;
    void on_action_success(ActionSuccessType success_type, std::string body) override;
    void on_action_fail(ActionFailType fail_type, std::string body) override;

    UserAccountSessionDispatchBase(const UserAccountSessionDispatchBase&)             = delete;
    UserAccountSessionDispatchBase(UserAccountSessionDispatchBase&& other)            = delete;
    UserAccountSessionDispatchBase& operator=(const UserAccountSessionDispatchBase&)  = delete;
    UserAccountSessionDispatchBase& operator=(UserAccountSessionDispatchBase&& other) = delete;

    void dispatch_printables_secret_token(const std::string& body);
protected:
    void dispatch_action_retry(const Network::IHttp::Retry& retry);
    void dispatch_action_success(ActionSuccessType success_type, std::string body);
    void dispatch_action_fail(ActionFailType fail_type, std::string body);
    void dispatch_enqueued_refresh();
    void dispatch_new_refresh_time(long long exp);
    void dispatch_race_lost(const std::string& body);
    void dispatch_logged_out(bool notify_owner, bool was_logged_in);

private:
    Platform::IMainThreadDispatcher& m_dispatcher;
    std::shared_ptr<bool> m_lifetime_token;
};
} // namespace Slic3r::Biz::UserAccount
