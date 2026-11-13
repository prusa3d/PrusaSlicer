#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountCommunication.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountSession.hpp"

#include <jthread/JThread.hpp>

#include <condition_variable>
#include <mutex>
#include <string>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief The session with its thread and the account data, shared by the Prusa Account (OAuth)
 * and the Connect Local (app pairing) communications.
 */
class UserAccountCommunicationBase : public IUserAccountCommunication
{
public:
    explicit UserAccountCommunicationBase(Platform::IMainThreadDispatcher& dispatcher);
    ~UserAccountCommunicationBase() override;

    UserAccountCommunicationBase(const UserAccountCommunicationBase&)            = delete;
    UserAccountCommunicationBase(UserAccountCommunicationBase&&)                 = delete;
    UserAccountCommunicationBase& operator=(const UserAccountCommunicationBase&) = delete;
    UserAccountCommunicationBase& operator=(UserAccountCommunicationBase&&)      = delete;

    bool is_active() const override
    {
        return true;
    }

    std::string
    wait_for_access_token(std::chrono::milliseconds timeout, JThread::StopToken stop_token) override
    {
        return m_session.wait_for_access_token(timeout, stop_token);
    }

    void add_session_listener(IUserAccountSessionListener* listener) override
    {
        m_session.add_listener<IUserAccountSessionListener>(listener);
    }

    std::string username() const override
    {
        return m_username;
    }

    std::string user_id() const override
    {
        return m_user_id;
    }

    bool is_logged_in() const override
    {
        return !m_username.empty();
    }

    std::string access_token() const override
    {
        return m_session.get_access_token();
    }

    std::string email() const override
    {
        return m_email;
    }

    void on_email(const std::string& data) override
    {
        m_email = data;
    }

    void cancel_ongoing_session_action() override
    {
        m_session.cancel_ongoing_session_action();
    }

    /**
     * @brief Returns path to avatar (even if it does not exists).
     */
    boost::filesystem::path avatar() const override;

    /**
     * @brief Uses data to download avatar.
     */
    void on_avatar_url(const std::string& data) override;

    /**
     * @brief Stores avatar into file.
     */
    void on_avatar_success(std::string&& data) const override;

protected:
    void init_session_thread();

    /**
     * @brief Wakes up the session thread. Needs to be called if the session should start
     * processing its action queue.
     */
    void wakeup_session_thread();

    /**
     * @brief Stops the session and joins its thread. A derived class whose session callbacks use
     * its own members calls it first in its destructor.
     */
    void stop_session_thread();

    UserAccountSession m_session;
    std::string m_username;
    std::string m_user_id;
    std::string m_email;

private:
    std::string m_avatar_extension;

    std::mutex m_thread_stop_mutex;
    std::condition_variable m_thread_stop_condition;
    bool m_thread_wakeup{true};
    JThread::JThread m_thread;
};

} // namespace Slic3r::Biz::UserAccount
