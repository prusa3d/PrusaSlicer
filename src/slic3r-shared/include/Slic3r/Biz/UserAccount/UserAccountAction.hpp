#pragma once

#include "Slic3r/Biz/UserAccount/UserAccountActionData.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"

#include <string>
#include <functional>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Interface of Action. Implementations perform POST or GET operation with callbacks.
 */
class IUserAccountAction
{
public:
    IUserAccountAction(const std::string name, const std::string url, bool requires_auth_token) : m_action_name(name), m_url(url), m_requires_auth_token(requires_auth_token){}
    virtual ~IUserAccountAction() = default;

    IUserAccountAction(const IUserAccountAction& ) = delete;
    IUserAccountAction(IUserAccountAction&& other) = delete;
    IUserAccountAction& operator=(const IUserAccountAction& ) = delete;
    IUserAccountAction& operator=(IUserAccountAction&& other) = delete;

    virtual void perform(
        IUserAccountActionCallbacks* callbacks,
        const std::string& access_token,
        ActionQueueData&& action_data,
        std::atomic_bool& global_cancel
    ) const = 0;

    bool get_requires_auth_token()
    {
        return m_requires_auth_token;
    }

protected:
    std::string m_action_name;
    std::string m_url;
    bool        m_requires_auth_token;
};

class UserAccountActionGetWithEvent : public IUserAccountAction
{
public:
    UserAccountActionGetWithEvent(const std::string name, const std::string url, ActionSuccessType success_type, ActionFailType fail_type, bool requires_auth_token = true)
        : IUserAccountAction(name, url, requires_auth_token)
        , m_success_type(success_type)
        , m_fail_type(fail_type)
    {}
    ~UserAccountActionGetWithEvent() {}

    void perform(
        IUserAccountActionCallbacks* callbacks,
        const std::string& access_token,
        ActionQueueData&& action_data,
        std::atomic_bool& global_cancel
    ) const override;

private:
    ActionSuccessType   m_success_type;
    ActionFailType      m_fail_type;
};

class UserAccountActionPost : public IUserAccountAction
{
public:
    UserAccountActionPost(const std::string name, const std::string url, bool requires_auth_token = true) : IUserAccountAction(name, url, requires_auth_token) {}
    ~UserAccountActionPost() {}

    void perform(
        IUserAccountActionCallbacks* callbacks,
        const std::string& access_token,
        ActionQueueData&& action_data,
        std::atomic_bool& global_cancel
    ) const override;
};

class UserAccountActionDummy : public IUserAccountAction
{
public:
    UserAccountActionDummy() : IUserAccountAction("Dummy", {}, false) {}
    ~UserAccountActionDummy() {}

    void perform(
        IUserAccountActionCallbacks* callbacks,
        const std::string& access_token,
        ActionQueueData&& action_data,
        std::atomic_bool& global_cancel
    ) const override
    {}
};
}