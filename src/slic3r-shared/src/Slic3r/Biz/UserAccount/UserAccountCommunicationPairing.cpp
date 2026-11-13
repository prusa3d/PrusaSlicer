#include "Slic3r/Biz/UserAccount/UserAccountCommunicationPairing.hpp"

#include "Slic3r/Biz/UserAccount/UserAccountTokenLog.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountBackgroundWorker.hpp"
#include "Slic3r/Biz/Network/IHttp.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Biz/Network/Url.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"

#include <boost/filesystem/path.hpp>

#include <algorithm>
#include <utility>

namespace Slic3r::Biz::UserAccount {

namespace {
/**
 * @brief The person is warned this long before the credential expires: they will have to sign in.
 */
constexpr std::time_t EXPIRATION_WARNING_SECONDS = 7 * 24 * 60 * 60;
/** @brief How often the expiration is checked, see check_expiration(). */
constexpr std::chrono::milliseconds EXPIRATION_CHECK_INTERVAL{60 * 60 * 1000};
/**
 * @brief A credential check the server did not answer is repeated after this, doubling up to the
 * max.
 */
constexpr std::chrono::seconds VERIFICATION_RETRY_MIN{60};
constexpr std::chrono::seconds VERIFICATION_RETRY_MAX{10 * 60};
/** @brief request_refresh() checks the credential at most this often. */
constexpr std::chrono::seconds REQUESTED_VERIFICATION_INTERVAL{30};
/**
 * @brief The session hands the token out until its deadline, this one for a token without
 * expiration.
 */
constexpr std::time_t UNKNOWN_EXPIRATION_SECONDS = 365 * 24 * 60 * 60;
/** @brief Revocation is best effort, it must not hold the app's shutdown for long. */
constexpr long REVOKE_CONNECT_TIMEOUT_SECONDS = 3;
constexpr long REVOKE_TOTAL_TIMEOUT_SECONDS   = 5;
/**
 * @brief A page session is handed out again while younger than this. The server keeps at most four
 * live page sessions per credential and opens at most 30 per 10 minutes for one address.
 */
constexpr std::chrono::minutes PAGE_SESSION_REUSE{10};

constexpr unsigned HTTP_UNAUTHORIZED = 401;

/**
 * @brief A POST to a Connect Local endpoint: JSON, without retries (the owner decides what next).
 */
ActionQueueData json_post(UserAccountActionID action_id, std::string body)
{
    ActionQueueData
        action{action_id, nullptr, nullptr, std::move(body), {{"Accept", "application/json"}}};
    if (!action.input.empty()) {
        action.additional_headers.emplace_back("Content-Type", "application/json");
    }
    action.retry = false;
    return action;
}
} // namespace

UserAccountCommunicationPairing::UserAccountCommunicationPairing(
    Platform::IMainThreadDispatcher& dispatcher
) :
    UserAccountCommunicationBase{dispatcher},
    m_dispatcher{dispatcher},
    m_verification_retry_delay{VERIFICATION_RETRY_MIN}
{}

UserAccountCommunicationPairing::~UserAccountCommunicationPairing()
{
    m_alive.reset();
    cancel_timer(m_poll_timer);
    cancel_timer(m_expiration_timer);
    cancel_timer(m_verification_retry_timer);
    // Session callbacks use members of this class.
    stop_session_thread();
    // m_revocation_worker joins when destroyed.
}

void UserAccountCommunicationPairing::cancel_timer(Platform::TimerQueue::TimerID& id)
{
    Platform::TimerQueue& queue = Platform::PlatformServices::instance().timer_queue();
    if (queue.is_timer_running(id)) {
        queue.cancel_timer(id);
    }
    id = Platform::TimerQueue::TimerID{};
}

void UserAccountCommunicationPairing::post(std::function<void()> fn)
{
    m_dispatcher.dispatch_on_main_thread(
        [alive = m_alive_weak, fn = std::move(fn)]()
        {
            if (alive.lock()) {
                fn();
            }
        }
    );
}

void UserAccountCommunicationPairing::add_session_listener(IUserAccountSessionListener* listener)
{
    UserAccountCommunicationBase::add_session_listener(listener);
    m_listeners.push_back(listener);
}

// ---- Credential ----

void UserAccountCommunicationPairing::init()
{
    init_session_thread();

    const TokenStore::OfflineCredentialRead read = TokenStore::read_offline_credential();
    if (read.status == TokenStore::StoreReadStatus::Error) {
        // Also the first start: the store does not tell a missing entry from a failure.
        SPDLOG_INFO("Connect Local: no credential could be read, starting signed out.");
        return;
    }
    if (read.status == TokenStore::StoreReadStatus::Empty) {
        SPDLOG_INFO("Connect Local: no stored credential.");
        return;
    }
    const TokenStore::OfflineCredential& stored = *read.credential;
    const std::string server = Network::ServiceConfig::instance().offline_server_url();
    if (stored.server_url != server) {
        // Kept for its server, which may be chosen again.
        SPDLOG_INFO(
            "Connect Local: the stored credential belongs to {}, not {} - not using it.",
            stored.server_url,
            server
        );
        return;
    }
    if (expired(stored)) {
        SPDLOG_INFO("Connect Local: the stored credential has expired - dropping it.");
        TokenStore::clear_offline_credential();
        return;
    }
    adopt(stored);
}

bool UserAccountCommunicationPairing::expired(const TokenStore::OfflineCredential& credential) const
{
    return credential.expires_at != 0 && credential.expires_at <= std::time(nullptr);
}

std::optional<TokenStore::OfflineCredential>
UserAccountCommunicationPairing::other_stored_credential(const std::string& token) const
{
    const TokenStore::OfflineCredentialRead read = TokenStore::read_offline_credential();
    if (read.status != TokenStore::StoreReadStatus::Ok) {
        return std::nullopt;
    }
    if (read.credential->server_url != Network::ServiceConfig::instance().offline_server_url()
        || read.credential->token == token
        || expired(*read.credential))
    {
        return std::nullopt;
    }
    return read.credential;
}

void
UserAccountCommunicationPairing::set_credential(const TokenStore::OfflineCredential& credential)
{
    SPDLOG_INFO(
        "Connect Local: using credential {} ({})",
        credential.credential_id,
        token_log_fingerprint(credential.token)
    );
    m_credential                       = credential;
    m_expiration_warned                = false;
    const std::time_t session_deadline = credential.expires_at != 0 ?
        credential.expires_at :
        std::time(nullptr) + UNKNOWN_EXPIRATION_SECONDS;
    m_session.set_tokens(credential.token, {}, {}, session_deadline);

    Platform::TimerQueue& timer_queue = Platform::PlatformServices::instance().timer_queue();
    if (!timer_queue.is_timer_running(m_expiration_timer)) {
        m_expiration_timer = timer_queue.set_timer(
            EXPIRATION_CHECK_INTERVAL,
            [this, alive = m_alive_weak]()
            {
                if (alive.lock()) {
                    check_expiration();
                }
            },
            true
        );
    }
}

void UserAccountCommunicationPairing::adopt(const TokenStore::OfflineCredential& credential)
{
    set_credential(credential);
    m_verification_retry_delay = VERIFICATION_RETRY_MIN;
    enqueue_verification();
}

void UserAccountCommunicationPairing::enqueue_verification()
{
    if (m_verification_in_flight) {
        return;
    }
    cancel_timer(m_verification_retry_timer);
    m_verification_in_flight = true;
    const std::string token  = m_session.get_access_token();
    // The account comes to the interactor like online (on_username_changed() or
    // on_user_id_unusable() follow). Without retries: a server that is down would block the queue.
    ActionQueueData action{UserAccountActionID::TestAccessToken, nullptr, nullptr, {}, {}};
    action.retry            = false;
    action.success_callback = [this, token](const std::string&)
    { post([this, token]() { on_verified(token); }); };
    action.fail_callback = [this, token](const std::string&, unsigned status)
    { post([this, token, status]() { on_verification_failed(token, status); }); };
    m_session.enqueue_priority_action(std::move(action));
    wakeup_session_thread();
}

void UserAccountCommunicationPairing::on_verified(const std::string& token)
{
    m_verification_in_flight = false;
    if (!token.empty() && token == m_session.get_access_token()) {
        m_verification_retry_delay = VERIFICATION_RETRY_MIN;
    }
}

void
UserAccountCommunicationPairing::on_verification_failed(const std::string& token, unsigned status)
{
    m_verification_in_flight = false;
    if (token.empty() || token != m_session.get_access_token()) {
        return;
    }
    if (status == HTTP_UNAUTHORIZED) {
        SPDLOG_INFO("Connect Local refused the credential.");
        on_credential_unusable(token);
        return;
    }
    // Server down or failing: keep the credential and try again later.
    SPDLOG_WARN("Connect Local: the credential could not be checked (HTTP {}).", status);
    if (m_awaiting_first_login && !m_account_unavailable_reported) {
        m_account_unavailable_reported = true;
        notify_pairing_failed(PairingFailure::AccountUnavailable);
    }
    retry_verification(token);
}

void UserAccountCommunicationPairing::on_user_id_unusable()
{
    const std::string token = m_session.get_access_token();
    if (token.empty()) {
        return;
    }
    if (m_awaiting_first_login) {
        // A credential of nobody the app can show: do not keep it.
        m_awaiting_first_login = false;
        log_out(false, true, true);
        notify_pairing_failed(PairingFailure::NoAccount);
        return;
    }
    // Not signed in yet, possibly a passing server fault: asked again like an unreachable server.
    retry_verification(token);
}

void UserAccountCommunicationPairing::retry_verification(const std::string& token)
{
    const std::chrono::seconds delay = m_verification_retry_delay;
    m_verification_retry_delay       = std::min(delay * 2, VERIFICATION_RETRY_MAX);
    SPDLOG_WARN("Connect Local: checking the credential again in {} s.", delay.count());
    cancel_timer(m_verification_retry_timer);
    m_verification_retry_timer = Platform::PlatformServices::instance().timer_queue().set_timer(
        delay,
        [this, alive = m_alive_weak, token]()
        {
            if (!alive.lock() || token != m_session.get_access_token() || is_logged_in()) {
                return;
            }
            enqueue_verification();
        },
        false
    );
}

void UserAccountCommunicationPairing::on_credential_unusable(const std::string& token)
{
    if (token.empty() || token != m_session.get_access_token()) {
        return;
    }
    if (const std::optional<TokenStore::OfflineCredential> other = other_stored_credential(token)) {
        // Another instance stored a newer credential meanwhile: carry on with that one.
        SPDLOG_INFO("Connect Local: switching to the credential in the store.");
        cancel_timer(m_verification_retry_timer);
        m_verification_in_flight = false;
        adopt(*other);
        return;
    }
    SPDLOG_INFO("Connect Local: the credential is not usable anymore - logging out.");
    log_out(true, false, true);
}

void UserAccountCommunicationPairing::
    on_username_changed(const std::string& username, const std::string& id, bool /*store*/)
{
    if (username.empty() && !m_session.get_access_token().empty()) {
        // A /me answer with an empty public username: no account the app can show.
        on_user_id_unusable();
        return;
    }
    if (!username.empty() && m_session.get_access_token().empty()) {
        // A late /me answer of a credential dropped meanwhile.
        SPDLOG_WARN("Connect Local: ignoring an account for a dropped credential.");
        return;
    }
    // The credential is stored when it is collected, nothing to write here.
    m_username = username;
    m_user_id  = id;
    m_session.set_has_username(!username.empty());
    if (!username.empty()) {
        m_awaiting_first_login         = false;
        m_account_unavailable_reported = false;
        check_expiration();
    }
}

void UserAccountCommunicationPairing::on_read_token_store_message()
{
    const TokenStore::OfflineCredentialRead read = TokenStore::read_offline_credential();
    if (read.status == TokenStore::StoreReadStatus::Error) {
        // Nothing is known, keep things as they are.
        return;
    }
    const std::string current = m_session.get_access_token();
    if (read.status == TokenStore::StoreReadStatus::Empty) {
        if (!current.empty() || is_logged_in()) {
            SPDLOG_INFO("Connect Local: another instance signed out - logging out.");
            // That instance revoked the credential and emptied the store already, and does not
            // need to be told back.
            log_out(false, false, false);
        }
        return;
    }
    const TokenStore::OfflineCredential& stored = *read.credential;
    if (stored.server_url != Network::ServiceConfig::instance().offline_server_url()
        || expired(stored)
        || stored.token == current)
    {
        return;
    }
    if (!current.empty() && stored.credential_id != m_credential.credential_id) {
        // Two instances paired at once and the other one stored its credential: ours is left over.
        SPDLOG_INFO("Connect Local: another instance stored its credential, revoking ours.");
        revoke(m_credential.credential_id, current);
    }
    SPDLOG_INFO("Connect Local: using the credential another instance stored.");
    cancel_pairing();
    adopt(stored);
}

void UserAccountCommunicationPairing::request_refresh()
{
    if (m_session.get_access_token().empty() || m_verification_in_flight) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (m_last_requested_verification != std::chrono::steady_clock::time_point{}
        && now - m_last_requested_verification < REQUESTED_VERIFICATION_INTERVAL)
    {
        return;
    }
    m_last_requested_verification = now;
    enqueue_verification();
}

bool UserAccountCommunicationPairing::validate_and_refresh()
{
    return check_expiration();
}

void UserAccountCommunicationPairing::on_unauthorized(const std::string& access_token)
{
    if (access_token.empty() || access_token != m_session.get_access_token()) {
        return;
    }
    SPDLOG_INFO("Connect Local refused the credential.");
    on_credential_unusable(access_token);
}

void UserAccountCommunicationPairing::on_avatar_url(const std::string& data)
{
    const std::string url =
        Network::resolve_url(data, Network::ServiceConfig::instance().account_me_url());
    const std::optional<Network::UrlParts> parts = Network::parse_url(url);
    if (!parts) {
        SPDLOG_ERROR("Connect Local sent an avatar URL that cannot be resolved: {}", data);
        return;
    }
    // The extension of the path only, a query is no part of the file name.
    const boost::filesystem::path server_file(parts->path);
    enqueue_avatar_download(url, server_file.extension().string());
}

bool UserAccountCommunicationPairing::check_expiration()
{
    const std::string token = m_session.get_access_token();
    if (token.empty() || m_credential.expires_at == 0) {
        return false;
    }
    const std::time_t now = std::time(nullptr);
    if (m_credential.expires_at <= now) {
        SPDLOG_INFO("Connect Local: the credential has expired.");
        on_credential_unusable(token);
        return true;
    }
    if (is_logged_in()
        && !m_expiration_warned
        && m_credential.expires_at - now <= EXPIRATION_WARNING_SECONDS)
    {
        m_expiration_warned = true;
        for (IUserAccountSessionListener* listener : m_listeners) {
            listener->on_credential_expires_soon(m_credential.expires_at);
        }
    }
    return false;
}

// ---- Logging out ----

void UserAccountCommunicationPairing::do_log_out(bool notify_owner)
{
    log_out(notify_owner, true, true);
}

void UserAccountCommunicationPairing::log_out_locally(bool notify_owner)
{
    log_out(notify_owner, false, false);
}

void
UserAccountCommunicationPairing::revoke(const std::string& credential_id, const std::string& token)
{
    const std::string url =
        Network::ServiceConfig::instance().offline_credential_url(credential_id);
    if (url.empty() || token.empty()) {
        return;
    }
    SPDLOG_INFO("Connect Local: revoking credential {}", credential_id);
    if (!m_revocation_worker) {
        m_revocation_worker = std::make_unique<BackgroundWorker>();
    }
    m_revocation_worker->add(
        [url, token, credential_id]()
        {
            std::unique_ptr<Network::IHttp> http = Network::IHttp::create(
                Network::IHttp::RequestMethod::Delete,
                url,
                [](Network::IHttp::Retry, bool& cancel) { cancel = false; }
            );
            http->timeout_connection(REVOKE_CONNECT_TIMEOUT_SECONDS)
                .timeout_total(REVOKE_TOTAL_TIMEOUT_SECONDS)
                .header("Authorization", "Bearer " + token)
                .on_complete(
                    [credential_id](std::string, unsigned status)
                    {
                        SPDLOG_INFO(
                            "Connect Local: credential {} revoked (HTTP {}).",
                            credential_id,
                            status
                        );
                    }
                )
                .on_error(
                    [credential_id](std::string, std::string error, unsigned status)
                    {
                        SPDLOG_WARN(
                            "Connect Local: revoking credential {} failed (HTTP {}): {}",
                            credential_id,
                            status,
                            error
                        );
                    }
                )
                .perform_sync(Network::HttpRetryOpt::no_retry());
        }
    );
}

void UserAccountCommunicationPairing::
    log_out(bool notify_owner, bool revoke_credential, bool clear_store)
{
    const bool awaiting_first_login = std::exchange(m_awaiting_first_login, false);
    cancel_pairing();
    const std::string token = m_session.get_access_token();
    if (!token.empty()) {
        if (revoke_credential) {
            revoke(m_credential.credential_id, token);
        }
        if (clear_store) {
            // Only if the store holds this credential, a newer one belongs to another instance.
            // When the store cannot be read, a revoked credential is cleared anyway.
            const TokenStore::OfflineCredentialRead read = TokenStore::read_offline_credential();
            if ((read.status == TokenStore::StoreReadStatus::Ok && read.credential->token == token)
                || (read.status == TokenStore::StoreReadStatus::Error && revoke_credential))
            {
                TokenStore::clear_offline_credential();
            }
        }
    }
    cancel_timer(m_expiration_timer);
    cancel_timer(m_verification_retry_timer);
    m_verification_in_flight       = false;
    m_verification_retry_delay     = VERIFICATION_RETRY_MIN;
    m_account_unavailable_reported = false;
    m_credential                   = {};
    m_expiration_warned            = false;
    m_page_session.reset();
    m_session.do_clear(notify_owner);
    m_username.clear();
    m_user_id.clear();
    m_email.clear();
    if (awaiting_first_login) {
        // The sign-in dialog waits for this credential.
        notify_pairing_failed(PairingFailure::CredentialLost);
    }
}

// ---- Pairing ----

void UserAccountCommunicationPairing::start_pairing()
{
    if (is_logged_in()) {
        // The dialog checks this itself before asking.
        SPDLOG_INFO("Connect Local: already signed in, no pairing needed.");
        return;
    }
    if (Network::ServiceConfig::instance().offline_pairing_url().empty()) {
        notify_pairing_failed(PairingFailure::ServerError);
        return;
    }
    if (!m_session.get_access_token().empty()) {
        // A credential the server never confirmed (e.g. unreachable at start): the new pairing
        // replaces it, and it must not win over the new one in on_credential().
        SPDLOG_INFO("Connect Local: dropping the unconfirmed credential before pairing again.");
        m_awaiting_first_login = false;
        log_out(false, true, true);
    }
    cancel_pairing();
    const uint64_t pairing_id = m_pairing_id;
    SPDLOG_INFO("Connect Local: starting an app pairing.");

    ActionQueueData action =
        json_post(UserAccountActionID::PairingRequest, Pairing::start_request_body());
    action.success_callback = [this, pairing_id](const std::string& body)
    { post([this, pairing_id, body]() { on_pairing_started(pairing_id, body); }); };
    action.fail_callback = [this, pairing_id](const std::string& body, unsigned status)
    {
        post([this, pairing_id, status, body]()
             { on_pairing_start_failed(pairing_id, status, body); });
    };
    m_session.enqueue_priority_action(std::move(action));
    wakeup_session_thread();
}

void UserAccountCommunicationPairing::cancel_pairing()
{
    ++m_pairing_id;
    cancel_timer(m_poll_timer);
}

void
UserAccountCommunicationPairing::on_pairing_started(uint64_t pairing_id, const std::string& body)
{
    if (pairing_id != m_pairing_id) {
        return;
    }
    const std::optional<Pairing::Started> started = Pairing::parse_started(body);
    if (!started) {
        SPDLOG_ERROR(
            "Connect Local: unreadable reply to the pairing request ({} bytes).",
            body.size()
        );
        notify_pairing_failed(PairingFailure::ServerError);
        return;
    }
    // The page is built from the server address the person chose, the verification_uri of the
    // reply comes from the Host header the server saw.
    const std::string page_url =
        Network::ServiceConfig::instance().offline_pairing_page_url(started->user_code);
    if (page_url.empty()) {
        SPDLOG_ERROR("Connect Local: the pairing reply has an unusable user code.");
        notify_pairing_failed(PairingFailure::ServerError);
        return;
    }
    m_poller.start(*started, std::time(nullptr));
    SPDLOG_INFO(
        "Connect Local: pairing started, waiting for approval (poll every {} s).",
        m_poller.interval()
    );
    for (IUserAccountSessionListener* listener : m_listeners) {
        listener->on_pairing_ready(page_url, started->user_code);
    }
    schedule_poll(m_poller.interval());
}

void UserAccountCommunicationPairing::
    on_pairing_start_failed(uint64_t pairing_id, unsigned status, const std::string& body)
{
    if (pairing_id != m_pairing_id) {
        return;
    }
    SPDLOG_WARN(
        "Connect Local: the pairing could not be started (HTTP {}, {}).",
        status,
        Pairing::error_code(body)
    );
    notify_pairing_failed(Pairing::failure_of(status, body));
}

void UserAccountCommunicationPairing::schedule_poll(int seconds)
{
    m_poll_timer = Platform::PlatformServices::instance().timer_queue().set_timer(
        std::chrono::seconds(seconds),
        [this, alive = m_alive_weak, pairing_id = m_pairing_id]()
        {
            if (alive.lock()) {
                poll(pairing_id);
            }
        },
        false
    );
}

void UserAccountCommunicationPairing::poll(uint64_t pairing_id)
{
    if (pairing_id != m_pairing_id) {
        return;
    }
    ActionQueueData action = json_post(
        UserAccountActionID::PairingPoll,
        Pairing::poll_request_body(m_poller.started().device_code)
    );
    action.success_callback = [this, pairing_id](const std::string& body)
    { post([this, pairing_id, body]() { on_poll_reply(pairing_id, 200, body); }); };
    action.fail_callback = [this, pairing_id](const std::string& body, unsigned status)
    { post([this, pairing_id, status, body]() { on_poll_reply(pairing_id, status, body); }); };
    m_session.enqueue_priority_action(std::move(action));
    wakeup_session_thread();
}

void UserAccountCommunicationPairing::
    on_poll_reply(uint64_t pairing_id, unsigned status, const std::string& body)
{
    if (pairing_id != m_pairing_id) {
        if (status >= 200 && status < 300) {
            // Approved after the pairing was cancelled: nobody uses it, do not leave it behind.
            if (const std::optional<Pairing::Credential> credential =
                    Pairing::parse_credential(body))
            {
                SPDLOG_INFO(
                    "Connect Local: a cancelled pairing was approved, revoking its credential."
                );
                revoke(credential->id, credential->token);
            } else {
                SPDLOG_ERROR("Connect Local: a cancelled pairing was approved, unreadably.");
            }
        }
        return;
    }
    const Pairing::Poller::Step step = m_poller.on_reply(status, body, std::time(nullptr));
    switch (step.kind) {
    case Pairing::Poller::Step::Kind::PollIn:
        schedule_poll(step.seconds);
        break;
    case Pairing::Poller::Step::Kind::Succeeded:
        on_credential(*step.credential);
        break;
    case Pairing::Poller::Step::Kind::Failed:
        SPDLOG_WARN(
            "Connect Local: pairing ended without a credential ({}, HTTP {}).",
            static_cast<int>(step.failure),
            status
        );
        if (step.credential) {
            // Issued but unusable (no operate scope): do not leave it behind on the server.
            revoke(step.credential->id, step.credential->token);
        }
        notify_pairing_failed(step.failure);
        break;
    }
}

void UserAccountCommunicationPairing::on_credential(const Pairing::Credential& credential)
{
    // Before /me is even asked: whoever cleans up after the approval page does it before the
    // account success is announced.
    for (IUserAccountSessionListener* listener : m_listeners) {
        listener->on_pairing_approved();
    }

    const std::string current = m_session.get_access_token();
    const std::optional<TokenStore::OfflineCredential> stored =
        other_stored_credential(credential.token);
    if (stored && stored->token != current) {
        // Another instance paired meanwhile and stored its credential first: use that one.
        // (Our own credential in the store, e.g. an unconfirmed one, never wins over a new one.)
        SPDLOG_INFO(
            "Connect Local: another instance signed in meanwhile, revoking our new credential."
        );
        revoke(credential.id, credential.token);
        m_awaiting_first_login = true;
        adopt(*stored);
        return;
    }

    const TokenStore::OfflineCredential offline{
        credential.token,
        credential.id,
        credential.expires_at,
        Network::ServiceConfig::instance().offline_server_url()
    };
    TokenStore::save_offline_credential(offline);
    m_awaiting_first_login         = true;
    m_account_unavailable_reported = false;
    // Checked like a stored credential: only a 401 drops it.
    adopt(offline);
}

void UserAccountCommunicationPairing::notify_pairing_failed(PairingFailure failure)
{
    for (IUserAccountSessionListener* listener : m_listeners) {
        listener->on_pairing_failed(failure);
    }
}

// ---- Requests of the other components ----

void UserAccountCommunicationPairing::request_printables_secret_token()
{
    m_session.dispatch_printables_secret_token({});
}

void UserAccountCommunicationPairing::enqueue_connect_printers_data_action(
    std::function<void(const std::string&)> succ_fn
)
{
    ActionQueueData action{UserAccountActionID::ConnectPrinterModels, std::move(succ_fn)};
    const std::string token = m_session.get_access_token();
    action.fail_callback    = [this, token](const std::string&, unsigned status)
    {
        if (status == HTTP_UNAUTHORIZED) {
            post([this, token]() { on_unauthorized(token); });
        }
    };
    m_session.enqueue_action(std::move(action));
    wakeup_session_thread();
}

void UserAccountCommunicationPairing::
    enqueue_page_session_action(std::function<void(std::optional<std::string>)> done, bool fresh)
{
    const std::string token = m_session.get_access_token();
    if (!fresh
        && m_page_session
        && m_page_session->token == token
        && std::chrono::steady_clock::now() - m_page_session->opened < PAGE_SESSION_REUSE)
    {
        post([done, reply = m_page_session->reply]() { done(reply); });
        return;
    }
    if (fresh) {
        // Asked for because a page had the recent one refused: not handed out any more.
        m_page_session.reset();
    }
    ActionQueueData action = json_post(UserAccountActionID::PageSession, {});
    action.additional_headers.emplace_back("Authorization", "Bearer " + token);
    action.success_callback = [this, token, done](const std::string& body)
    {
        post(
            [this, token, done, body]()
            {
                if (token == m_session.get_access_token()) {
                    m_page_session = PageSession{token, body, std::chrono::steady_clock::now()};
                }
                done(body);
            }
        );
    };
    action.fail_callback = [this, token, done](const std::string& body, unsigned status)
    {
        const Pairing::PageSessionError error = Pairing::classify_page_session_error(status, body);
        if (error == Pairing::PageSessionError::Unauthorized) {
            post([this, token]() { on_unauthorized(token); });
            return;
        }
        SPDLOG_WARN(
            "Connect Local page session refused (HTTP {}, {}).",
            status,
            Pairing::error_code(body)
        );
        post([done]() { done(std::nullopt); });
    };
    m_session.enqueue_action(std::move(action));
    wakeup_session_thread();
}

} // namespace Slic3r::Biz::UserAccount
