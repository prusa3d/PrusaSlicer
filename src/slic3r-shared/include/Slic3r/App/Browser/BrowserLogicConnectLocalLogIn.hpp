#pragma once

#include "Slic3r/App/Browser/AbstractBrowserLogic.hpp"
#include "Slic3r/Biz/Network/ServerTrustProbe.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Slic3r::Biz::UserAccount {
class UserAccountInteractor;
} // namespace Slic3r::Biz::UserAccount

namespace Slic3r::App::Browser {

/**
 * @brief Sign-in dialog of the offline mode: Connect Local app pairing.
 *
 * Checks the server certificate (https), starts the pairing and opens the server's approval
 * page in the dialog. If that page does not show up in time, the code is shown on a local page
 * to be approved in a browser. The account signs in by polling, the dialog ends with it. The
 * session cookie the approval page leaves in the web views is deleted before the account is
 * announced, or when the dialog ends without signing in.
 */
class BrowserLogicConnectLocalLogIn final : public AbstractBrowserLogic
{
public:
    explicit BrowserLogicConnectLocalLogIn(Biz::UserAccount::UserAccountInteractor& user_account);
    ~BrowserLogicConnectLocalLogIn() override;

    std::pair<int, int> size(int em_unit) const override
    {
        return {50 * em_unit, 80 * em_unit};
    }

    std::vector<BrowserLogicCommand> on_loaded_webview_event(const std::string& url) override;
    std::vector<BrowserLogicCommand>
    on_script_message_webview_event(const std::string& message) override;
    std::vector<BrowserLogicCommand>
    on_user_account_id_success(bool is_refresh, const std::string& current_url) override;
    std::vector<BrowserLogicCommand> on_user_account_pairing_ready(
        const std::string& page_url,
        const std::string& user_code
    ) override;
    std::vector<BrowserLogicCommand>
    on_user_account_pairing_failed(Biz::UserAccount::PairingFailure failure) override;
    std::vector<BrowserLogicCommand> on_user_account_pairing_approved() override;
    std::vector<BrowserLogicCommand>
    on_load_error(const std::string& url, bool certificate_error) override;
    std::vector<BrowserLogicCommand> on_dialog_closing() override;

private:
    enum class State
    {
        /** @brief Checking the server and starting the pairing. */
        Starting,
        /** @brief The server's approval page is open in this dialog. */
        PageOpen,
        /** @brief The code is shown on the local page, approval happens in a browser. */
        Waiting,
        Failed,
        Done,
    };

    std::vector<BrowserLogicCommand> page_init_commands() const;
    std::vector<BrowserLogicCommand> page_state_commands() const;
    std::vector<BrowserLogicCommand> show_error(const std::string& reason, bool can_retry = true);
    std::vector<BrowserLogicCommand> show_waiting(const std::string& note);
    /** @brief Checks the server certificate (https), then starts the pairing. */
    void start_sign_in();
    void request_pairing();
    /**
     * @brief Deletes the server session (pcl_session) the approval page left in the web views'
     * cookies, if this dialog loaded that page and has not deleted it yet, and nobody is signed in.
     */
    std::vector<BrowserLogicCommand> take_session_cookie_deletion();
    /** @brief The dialog ends without a sign-in: stops the pairing, drops the session cookie. */
    std::vector<BrowserLogicCommand> abandon_sign_in();

    Biz::UserAccount::UserAccountInteractor& m_user_account;
    bool m_page_requested{false};

    State m_state{State::Starting};
    /** @brief Why signing in failed; shown by the local page. */
    std::string m_error;
    bool m_error_retry{true};
    /** @brief Shown under the code while waiting. */
    std::string m_waiting_note;
    std::string m_pairing_url;
    std::string m_user_code;
    /** @brief This dialog loaded a server page whose session cookie is not deleted yet. */
    bool m_session_cookie_pending{false};
    /** @brief Checks the server certificate before the pairing starts. */
    Biz::Network::ServerTrustProbe m_certificate_probe{"Connect Local sign-in certificate check"};
    /** @brief Alive while waiting for the server page; dropping it cancels the timeout. */
    std::shared_ptr<char> m_page_timeout_token;
    std::shared_ptr<char> m_alive{std::make_shared<char>(0)};
};

} // namespace Slic3r::App::Browser
