#include "Slic3r/Biz/UserAccount/UserAccountSessionDispatchBase.hpp"

namespace Slic3r::Biz::UserAccount {

UserAccountSessionDispatchBase::UserAccountSessionDispatchBase(Platform::IMainThreadDispatcher& dispatcher) :
    m_dispatcher{dispatcher},
    m_lifetime_token{std::make_shared<bool>(true)}
{}

UserAccountSessionDispatchBase::~UserAccountSessionDispatchBase()
{
    // Intentionally empty.
    // We no longer require m_dispatcher.is_closed() because we support dynamic runtime 
    // destruction. Any pending dispatcher lambdas are safely caught and aborted 
    // using the m_lifetime_token weak_ptr checks.
}

void UserAccountSessionDispatchBase::on_action_retry(const Network::IHttp::Retry& retry)
{
    dispatch_action_retry(retry);
}

void UserAccountSessionDispatchBase::on_action_success(ActionSuccessType success_type, std::string body)
{
    dispatch_action_success(success_type, std::move(body));
}

void UserAccountSessionDispatchBase::on_action_fail(ActionFailType fail_type, std::string body)
{
    dispatch_action_fail(fail_type, std::move(body));
}

void UserAccountSessionDispatchBase::dispatch_action_retry(const Network::IHttp::Retry& retry)
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token, retry]()
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [retry](auto* listener) { listener->on_action_retry(retry); }
            );
        }
    );
}

void UserAccountSessionDispatchBase::dispatch_action_success(ActionSuccessType success_type, std::string body)
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.    
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token, success_type, body = std::move(body)]() mutable
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [success_type, body = std::move(body)](auto* listener)
                { listener->on_action_success(success_type, body); }
            );
        }
    );
}

void UserAccountSessionDispatchBase::dispatch_action_fail(ActionFailType fail_type, std::string body)
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token, fail_type, body = std::move(body)]() mutable
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [fail_type, body = std::move(body)](auto* listener)
                { listener->on_action_fail(fail_type, body); }
            );
        }
    );
}

void UserAccountSessionDispatchBase::dispatch_enqueued_refresh()
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token]()
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [](auto* listener) { listener->on_enqueued_refresh(); }
            );
        }
    );
}

void UserAccountSessionDispatchBase::dispatch_new_refresh_time(long long exp)
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token, exp]()
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [exp](auto* listener) { listener->on_new_refresh_time(exp); }
            );
        }
    );
}

void UserAccountSessionDispatchBase::dispatch_race_lost(const std::string& body)
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token, body]()
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [body](auto* listener) { listener->on_race_lost(body); }
            );
        }
    );
}

void UserAccountSessionDispatchBase::dispatch_logged_out(bool notify_owner, bool was_logged_in)
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token, notify_owner, was_logged_in]()
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [notify_owner, was_logged_in](auto* listener)
                { listener->on_logged_out(notify_owner, was_logged_in); }
            );
        }
    );
}

void UserAccountSessionDispatchBase::dispatch_printables_secret_token(const std::string& body)
{
    // User Account could be turned off during runtime, which destroys most of its objects including this one.
    std::weak_ptr<bool> weak_token = m_lifetime_token;
    m_dispatcher.dispatch_on_main_thread(
        [this, weak_token, body]()
        {
            if (!weak_token.lock()) return;
            this->invoke_listeners<IUserAccountSessionListener>(
                [body](auto* listener) { listener->on_printables_secret_token(body); }
            );
        }
    );
}

} // namespace Slic3r::Biz::UserAccount