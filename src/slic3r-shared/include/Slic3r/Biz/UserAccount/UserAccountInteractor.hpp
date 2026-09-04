#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountCommunication.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountSessionListener.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <functional>
#include <map>

namespace Slic3r::Biz {
class ProjectInteractor;
}
namespace Slic3r::Biz::UserAccount {
/**
 * @brief Hub for all communication with UserAccount.
 */
class UserAccountInteractor final : public IUserAccountSessionListener, public WithListeners<IUserAccountListener>
{
public:
    UserAccountInteractor(Platform::IMainThreadDispatcher& dispatcher);
    ~UserAccountInteractor();

    UserAccountInteractor(const UserAccountInteractor&)             = delete;
    UserAccountInteractor(UserAccountInteractor&& other)            = delete;
    UserAccountInteractor& operator=(const UserAccountInteractor&)  = delete;
    UserAccountInteractor& operator=(UserAccountInteractor&& other) = delete;

    /**
     * @brief If enabled reads tokens from store, starts communication in bg thread. Otherwise logsout and completely disables communication by replacing its implementation.
     */
    void init(bool app_config_enabled);

    /**
     * @brief Returns true if communication was initilized.
     */
    bool is_enabled() const;

    /**
     * @brief Logs out of User Account, tokens are thrown out, all other running apps gets message to log out.
     */
    void do_log_out();

    /**
     * @brief Returns url to be displayed in browser for logging in.
     * @param lang_code Language code for localization. 2 letters.
     * @param generate_code_verifier If true, code verifier is generated and stored for later use. If false, older verifier is used. Should be false if service is not Prusa Account.
     * @param service Service that user selected to log in with. Returned url will be redirecting to the service. Default empty service is Prusa Account log in.
     * Use case: User selects "log in" - this function is called to get url to be displayed in dialog.
     * There user selects "Google" - This function is called with "Google" as service and returns url to be displayed in external browser.
     */
    std::string
    on_log_in_request(const std::string& lang_code, bool generate_code_verifier, const std::string& service = std::string());

    /**
     * @brief Passes code from browser to finish logging in.
     */
    void on_log_in_code_response(const std::string& url_message);

    /**
     * @brief Returns true if logging in is finalized.
     */
    bool is_logged_in() const;

    /**
     * @brief Called when other instance changed tokens in store.
     */
    void on_read_token_store_message();

    /**
     * @brief Returns public username or empty string.
     */
    std::string username() const;

    /**
     * @brief Returns path to avatar (even if it does not exists).
     */
    boost::filesystem::path avatar() const;

    std::string email() const;

    /**
     * @brief Sets callback for refreshing left bar account menu. Bool is recreate avatar.
     */
    void set_update_menu_callback(std::function<void(bool)> cb)
    {
        update_menu_callback = std::move(cb);
    }

    void cancel_ongoing_session_action()
    {
        m_communication->cancel_ongoing_session_action();
    }

    /**
     * @brief Starts refresh sequence of access token
     */
    void request_refresh();

    /**
     * @brief Starts refresh sequence of access token if needed. Returns true if refresh started.
     */
    bool validate_and_refresh();

    // IUserAccountSessionListener implementations
    void on_action_retry(const Network::IHttp::Retry& retry) override;
    void on_action_success(ActionSuccessType success_type, std::string body) override;
    void on_action_fail(ActionFailType fail_type, std::string body) override;
    void on_enqueued_refresh() override;
    void on_new_refresh_time(long long exp) override;
    void on_race_lost(const std::string& body) override;
    void on_logged_out(bool notify_owner, bool was_logged_in) override;
    void on_printables_secret_token(const std::string& body) override;

    std::string access_token() const;

    void request_printables_secret_token();

private:
    void on_user_id(const std::string& body);
    void notify_action_retry_finished();

    Platform::IMainThreadDispatcher& m_dispatcher;
    std::unique_ptr<IUserAccountCommunication> m_communication;

    std::map<std::string, std::string> m_account_user_data;

    std::function<void(bool)> update_menu_callback;
};
} // namespace Slic3r::Biz::UserAccount
