#include "Slic3r/App/Browser/BrowserLogicConnectLocalLogIn.hpp"

#include "Slic3r/App/ConnectLocalTrust.hpp"
#include "Slic3r/App/Browser/BrowserCookie.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/JsonForScript.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Log.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <chrono>

namespace Slic3r::App::Browser {

namespace {
/** @brief Local page of the dialog: starting, waiting for approval and errors. */
const std::string LOGIN_PAGE = "connect_local_login";
/** @brief The server's approval page has this long to show up, else the code is shown locally. */
constexpr std::chrono::seconds APPROVAL_PAGE_TIMEOUT{15};

std::string failure_reason(Biz::UserAccount::PairingFailure failure)
{
    using Biz::UserAccount::PairingFailure;
    switch (failure) {
    case PairingFailure::Denied:
        return Biz::_u8L("The sign-in request was denied on the server.");
    case PairingFailure::Expired:
        return Biz::_u8L("The code expired before it was approved.");
    case PairingFailure::Unknown:
        return Biz::_u8L(
            "The server does not know this sign-in request anymore, it may have been restarted."
        );
    case PairingFailure::Limited:
        return Biz::_u8L(
            "Too many sign-in attempts from this computer. Wait a few minutes and try again."
        );
    case PairingFailure::NoOperateScope:
        return Biz::_u8L(
            "The approval does not allow printing. PrusaSlicer needs the permission to operate printers to send prints, approve it with that permission."
        );
    case PairingFailure::Unreachable:
        return Biz::_u8L("Cannot connect to the server.");
    case PairingFailure::ServerError:
        return Biz::_u8L("The server answered unexpectedly.");
    case PairingFailure::CredentialLost:
        return Biz::_u8L("The sign-in was approved, but the server no longer accepts it.");
    case PairingFailure::NoAccount:
        return Biz::_u8L("The sign-in was approved, but the server has no account to show for it.");
    case PairingFailure::AccountUnavailable:
        return Biz::_u8L(
            "The sign-in was approved, but your account cannot be loaded from the server yet. PrusaSlicer keeps trying and finishes signing in by itself."
        );
    }
    return {};
}
} // namespace

BrowserLogicConnectLocalLogIn::BrowserLogicConnectLocalLogIn(
    Biz::UserAccount::UserAccountInteractor& user_account
) :
    AbstractBrowserLogic({}, {"PrusaSlicerWebviewMessage"}, LOGIN_PAGE),
    m_user_account(user_account)
{
    set_title("Sign in");
}

BrowserLogicConnectLocalLogIn::~BrowserLogicConnectLocalLogIn()
{
    if (m_state != State::Done) {
        // Closed without signing in, whatever way: the pairing would go on in the background.
        m_user_account.cancel_pairing();
    }
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::page_init_commands() const
{
    const nlohmann::json texts = {
        {"opening", Biz::_u8L("Connecting to your Prusa Connect (on-premise) server…")},
        {"waitingTitle", Biz::_u8L("Approve PrusaSlicer on your Prusa Connect (on-premise) server")},
        {"waitingHint", Biz::_u8L("Sign in on the server and approve this code:")},
        {"waitingStatus", Biz::_u8L("Waiting for approval…")},
        {"openInBrowser", Biz::_u8L("Open in browser")},
        {"failedTitle", Biz::_u8L("Signing in did not succeed")},
        {"hint",
         Biz::_u8L(
             "Check that the server is running, or choose another server with Change server in the Connect tab."
         )},
        {"retry", Biz::_u8L("Try again")},
        {"cancel", Biz::_u8L("Cancel")},
        {"close", Biz::_u8L("Close")},
    };
    return {
        {BrowserLogicCommandType::RunScript,
         "window.connectLocalLogin && window.connectLocalLogin.init("
             + Biz::json_for_script(texts)
             + ");"}
    };
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::page_state_commands() const
{
    switch (m_state) {
    case State::Waiting:
        return {
            {BrowserLogicCommandType::RunScript,
             "window.connectLocalLogin && window.connectLocalLogin.showWaiting("
                 + Biz::json_for_script(m_user_code)
                 + ", "
                 + Biz::json_for_script(m_waiting_note)
                 + ");"}
        };
    case State::Failed:
        return {
            {BrowserLogicCommandType::RunScript,
             "window.connectLocalLogin && window.connectLocalLogin.showError("
                 + Biz::json_for_script(m_error)
                 + ", "
                 + (m_error_retry ? "true" : "false")
                 + ");"}
        };
    default:
        return {};
    }
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::take_session_cookie_deletion()
{
    // Only the session of the approval page this dialog loaded, at most once, and never once the
    // account is signed in: the Connect tab's page session has the same cookie name.
    if (!std::exchange(m_session_cookie_pending, false) || m_user_account.is_logged_in()) {
        return {};
    }
    return {
        {BrowserLogicCommandType::DeleteCookie,
         cookie_command_data(
             CONNECT_LOCAL_SESSION_COOKIE,
             Biz::Network::ServiceConfig::instance().offline_server_url()
         )}
    };
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::on_user_account_pairing_approved()
{
    // The approval page is done with. Its session goes now, before the account loads and the
    // Connect tab opens its own page session under the same cookie name.
    return take_session_cookie_deletion();
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::abandon_sign_in()
{
    if (m_state != State::Done) {
        m_user_account.cancel_pairing();
    }
    m_state = State::Done;
    m_page_timeout_token.reset();
    return take_session_cookie_deletion();
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::on_dialog_closing()
{
    return abandon_sign_in();
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectLocalLogIn::show_error(const std::string& reason, bool can_retry)
{
    SPDLOG_WARN("Connect Local sign-in: {}", reason);
    m_page_timeout_token.reset();
    m_state       = State::Failed;
    m_error       = reason;
    m_error_retry = can_retry;
    return {{BrowserLogicCommandType::LoadResourcesPage, LOGIN_PAGE}};
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectLocalLogIn::show_waiting(const std::string& note)
{
    m_page_timeout_token.reset();
    m_state        = State::Waiting;
    m_waiting_note = note;
    return {{BrowserLogicCommandType::LoadResourcesPage, LOGIN_PAGE}};
}

void BrowserLogicConnectLocalLogIn::request_pairing()
{
    m_state = State::Starting;
    // Not from within a webview event: the pairing may fail right away, and its commands must
    // not mix with those of the event.
    Biz::Platform::PlatformServices::instance().main_thread_dispatcher().dispatch_on_main_thread(
        [alive = std::weak_ptr<char>(m_alive), this]()
        {
            if (alive.expired() || m_state != State::Starting) {
                return;
            }
            if (m_user_account.is_logged_in()) {
                // Signed in meanwhile (e.g. by another instance): nothing to pair.
                m_state = State::Done;
                push_commands({{BrowserLogicCommandType::EndModalOK, {}}});
                return;
            }
            m_user_account.start_pairing();
        }
    );
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::
    on_user_account_pairing_ready(const std::string& page_url, const std::string& user_code)
{
    if (m_state != State::Starting) {
        return {};
    }
    m_pairing_url = page_url;
    m_user_code   = user_code;
    SPDLOG_INFO("Connect Local sign-in: opening {}", m_pairing_url);
    m_state              = State::PageOpen;
    m_page_timeout_token = std::make_shared<char>(0);
    Biz::Platform::PlatformServices::instance().timer_queue().set_timer(
        APPROVAL_PAGE_TIMEOUT,
        [this, token = std::weak_ptr<char>(m_page_timeout_token)]()
        {
            if (token.expired()) {
                return;
            }
            SPDLOG_WARN("Connect Local sign-in: the approval page did not load in time.");
            push_commands(show_waiting(
                Biz::_u8L("The approval page did not load here. Open it in your browser.")
            ));
        },
        false
    );
    return {{BrowserLogicCommandType::LoadURL, m_pairing_url}};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::on_user_account_pairing_failed(
    Biz::UserAccount::PairingFailure failure
)
{
    // A final error stays; one that waits for the account in the background may be followed by
    // the final outcome.
    if (m_state == State::Done || (m_state == State::Failed && m_error_retry)) {
        return {};
    }
    // Trying again would only start another pairing while the approved one is still being loaded.
    const bool can_retry = failure != Biz::UserAccount::PairingFailure::AccountUnavailable;
    std::vector<BrowserLogicCommand> commands = take_session_cookie_deletion();
    std::vector<BrowserLogicCommand> error    = show_error(failure_reason(failure), can_retry);
    commands.insert(commands.end(), error.begin(), error.end());
    return commands;
}

void BrowserLogicConnectLocalLogIn::start_sign_in()
{
    m_state                  = State::Starting;
    const std::string server = Biz::Network::ServiceConfig::instance().offline_server_url();
    if (server.empty()) {
        push_commands(show_error(Biz::_u8L("No Prusa Connect (on-premise) server is set."), false));
        return;
    }
    if (!server.starts_with("https://")) {
        request_pairing();
        return;
    }
    // https: check the certificate first, a refused one would leave the dialog blank.
    SPDLOG_INFO("Connect Local sign-in: checking the certificate of {}", server);
    m_certificate_probe.start(
        server,
        ConnectLocalTrust::trust_for_url(server).value_or(Biz::Network::ServerTrust{}),
        [this](Biz::Network::TlsProbeResult result)
        {
            switch (result.status) {
            case Biz::Network::TlsProbeStatus::Ok:
                request_pairing();
                break;
            case Biz::Network::TlsProbeStatus::CertificateRejected:
                push_commands(show_error(
                    fmt::format(
                        fmt::runtime(
                            Biz::_u8L(
                                // TRN {} is the technical reason, e.g. "SSL certificate problem: self-signed certificate".
                                "The server certificate is not trusted ({}). Trust it in the Connect tab first."
                            )
                        ),
                        result.error
                    ),
                    false
                ));
                break;
            case Biz::Network::TlsProbeStatus::Unreachable:
                push_commands(show_error(
                    fmt::format(
                        // TRN {} is the technical reason, e.g. "Failed to connect to 192.168.1.10 port 8443".
                        fmt::runtime(Biz::_u8L("Cannot connect to the server: {}")),
                        result.error
                    )
                ));
                break;
            }
        }
    );
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectLocalLogIn::on_load_error(const std::string& url, bool certificate_error)
{
    if (m_state == State::PageOpen) {
        // The pairing itself still waits: let the person approve it in a browser.
        return show_waiting(
            certificate_error ?
                Biz::_u8L(
                    "The approval page was refused here because of its certificate. Open it in your browser."
                ) :
                Biz::_u8L("The approval page could not be loaded here. Open it in your browser.")
        );
    }
    if (m_state == State::Failed || m_state == State::Waiting) {
        return {};
    }
    return show_error(
        certificate_error ?
            Biz::_u8L("The server certificate was refused.") :
            // TRN {} is the address that failed to load.
            fmt::format(fmt::runtime(Biz::_u8L("The page {} could not be loaded.")), url)
    );
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectLocalLogIn::on_loaded_webview_event(const std::string& url)
{
    if (url.empty()) {
        return {};
    }
    if (url.find("/web/" + m_loading_html) != std::string::npos) {
        std::vector<BrowserLogicCommand> commands = page_init_commands();
        std::vector<BrowserLogicCommand> state    = page_state_commands();
        commands.insert(commands.end(), state.begin(), state.end());
        if (!std::exchange(m_page_requested, true)) {
            start_sign_in();
        }
        return commands;
    }
    if (!url.starts_with("file:")) {
        if (m_state == State::PageOpen) {
            // The approval page (or the server's sign-in on the way to it) is in this dialog: a
            // session cookie may now exist that is this dialog's to delete.
            m_session_cookie_pending = true;
        }
        if (m_page_timeout_token) {
            SPDLOG_INFO("Connect Local sign-in: page loaded {}", url);
            m_page_timeout_token.reset();
        }
    }
    return {};
}

std::vector<BrowserLogicCommand>
BrowserLogicConnectLocalLogIn::on_script_message_webview_event(const std::string& message)
{
    std::string event;
    try {
        const nlohmann::json j = nlohmann::json::parse(message);
        if (j.contains("event") && j["event"].is_string()) {
            event = j["event"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse WebView message. {}", e.what());
        return {};
    }
    if (event == "closeDialog") {
        // The person may have signed in on the approval page before giving up.
        std::vector<BrowserLogicCommand> commands = abandon_sign_in();
        commands.emplace_back(BrowserLogicCommandType::EndModalCancel, std::string());
        return commands;
    }
    if (event == "openInBrowser" && !m_pairing_url.empty()) {
        return {{BrowserLogicCommandType::OpenExternalBrowserForced, m_pairing_url}};
    }
    if (event == "retry") {
        m_error.clear();
        start_sign_in();
        return {
            {BrowserLogicCommandType::RunScript,
             "window.connectLocalLogin && window.connectLocalLogin.showOpening();"}
        };
    }
    return {};
}

std::vector<BrowserLogicCommand> BrowserLogicConnectLocalLogIn::
    on_user_account_id_success(bool is_refresh, const std::string& current_url)
{
    // The approval page's session was deleted when the credential was collected, before this
    // broadcast; deleting now could hit the Connect tab's page session (same cookie name).
    m_state = State::Done;
    m_page_timeout_token.reset();
    m_session_cookie_pending = false;
    return {{BrowserLogicCommandType::EndModalOK, {}}};
}

} // namespace Slic3r::App::Browser
