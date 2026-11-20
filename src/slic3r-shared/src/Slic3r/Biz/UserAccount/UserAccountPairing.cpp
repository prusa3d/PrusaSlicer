#include "Slic3r/Biz/UserAccount/UserAccountPairing.hpp"

#include "Slic3r/Biz/Network/Jwt.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace Slic3r::Biz::UserAccount::Pairing {

namespace {
/** @brief Lifetime of a pairing when the server does not say (the server default). */
constexpr int DEFAULT_EXPIRES_IN_SECONDS = 600;

constexpr std::string_view CODE_PENDING   = "core.auth/APP_PAIRING_PENDING";
constexpr std::string_view CODE_SLOW_DOWN = "core.auth/APP_PAIRING_SLOW_DOWN";
constexpr std::string_view CODE_DENIED    = "core.auth/APP_PAIRING_DENIED";
constexpr std::string_view CODE_EXPIRED   = "core.auth/APP_PAIRING_EXPIRED";
constexpr std::string_view CODE_UNKNOWN   = "core.auth/APP_PAIRING_UNKNOWN";
constexpr std::string_view CODE_LIMITED   = "core.auth/APP_PAIRING_LIMITED";

constexpr std::string_view CODE_TOKEN_INVALID   = "core.auth/TOKEN_INVALID";
constexpr std::string_view CODE_SESSION_REFUSED = "core.auth/PAGE_SESSION_REFUSED";
constexpr std::string_view CODE_SESSION_LIMITED = "core.auth/PAGE_SESSION_LIMITED";

std::optional<nlohmann::json> parse_object(const std::string& body)
{
    try {
        nlohmann::json j = nlohmann::json::parse(body);
        if (j.is_object()) {
            return j;
        }
    } catch (const nlohmann::json::exception&) {
    }
    return std::nullopt;
}

std::string string_field(const nlohmann::json& j, const char* key)
{
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<long long>());
    }
    return {};
}

int int_field(const nlohmann::json& j, const char* key, int fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number()) {
        return fallback;
    }
    return static_cast<int>(it->get<double>());
}

PairingFailure failure_of_code(unsigned http_status, const std::string& code)
{
    if (code == CODE_DENIED) {
        return PairingFailure::Denied;
    }
    if (code == CODE_EXPIRED) {
        return PairingFailure::Expired;
    }
    if (code == CODE_UNKNOWN) {
        return PairingFailure::Unknown;
    }
    if (code == CODE_LIMITED || http_status == 429) {
        return PairingFailure::Limited;
    }
    if (http_status == 0 || http_status == 408 || http_status >= 500) {
        return PairingFailure::Unreachable;
    }
    return PairingFailure::ServerError;
}

/**
 * @brief The server always lists the scopes of a credential. One that does not, or lacks the
 * operate scope, cannot be shown to allow printing and is refused.
 */
bool has_operate_scope(const Credential& credential)
{
    return credential.scopes
        && std::ranges::find(*credential.scopes, OPERATE_SCOPE) != credential.scopes->end();
}
} // namespace

std::string start_request_body()
{
    nlohmann::json j;
    j["app_name"] = APP_NAME;
    j["scopes"]   = nlohmann::json::array({OPERATE_SCOPE});
    return j.dump();
}

std::string poll_request_body(const std::string& device_code)
{
    nlohmann::json j;
    j["device_code"] = device_code;
    return j.dump();
}

std::optional<Started> parse_started(const std::string& body)
{
    const std::optional<nlohmann::json> j = parse_object(body);
    if (!j) {
        return std::nullopt;
    }
    Started started;
    started.device_code = string_field(*j, "device_code");
    started.user_code   = string_field(*j, "user_code");
    if (started.device_code.empty() || started.user_code.empty()) {
        return std::nullopt;
    }
    started.expires_in = int_field(*j, "expires_in", DEFAULT_EXPIRES_IN_SECONDS);
    started.interval   = int_field(*j, "interval", DEFAULT_INTERVAL_SECONDS);
    return started;
}

std::optional<Credential> parse_credential(const std::string& body)
{
    const std::optional<nlohmann::json> j = parse_object(body);
    if (!j) {
        return std::nullopt;
    }
    Credential credential;
    credential.id    = string_field(*j, "id");
    credential.token = string_field(*j, "access_token");
    if (credential.id.empty() || credential.token.empty()) {
        return std::nullopt;
    }
    if (const boost::optional<double> exp = Network::Jwt::get_exp(credential.token)) {
        credential.expires_at = static_cast<std::time_t>(*exp);
    } else if (const auto it = j->find("expires_at"); it != j->end() && it->is_number()) {
        credential.expires_at = static_cast<std::time_t>(it->get<double>());
    }
    if (const auto it = j->find("scopes"); it != j->end() && it->is_array()) {
        credential.scopes.emplace();
        for (const nlohmann::json& scope : *it) {
            if (scope.is_string()) {
                credential.scopes->push_back(scope.get<std::string>());
            }
        }
    }
    return credential;
}

std::string error_code(const std::string& body)
{
    const std::optional<nlohmann::json> j = parse_object(body);
    if (!j) {
        return {};
    }
    if (std::string code = string_field(*j, "code"); !code.empty()) {
        return code;
    }
    if (const auto it = j->find("error"); it != j->end() && it->is_object()) {
        return string_field(*it, "code");
    }
    return {};
}

PairingFailure failure_of(unsigned http_status, const std::string& body)
{
    return failure_of_code(http_status, error_code(body));
}

PageSessionError classify_page_session_error(unsigned http_status, const std::string& body)
{
    const std::string code = error_code(body);
    if (http_status == 401 || code == CODE_TOKEN_INVALID) {
        return PageSessionError::Unauthorized;
    }
    if (http_status == 429 || code == CODE_SESSION_LIMITED) {
        return PageSessionError::Limited;
    }
    if (http_status == 403 || code == CODE_SESSION_REFUSED) {
        return PageSessionError::Refused;
    }
    return PageSessionError::Failed;
}

void Poller::start(const Started& started, std::time_t now)
{
    m_started  = started;
    m_interval = started.interval > 0 ? started.interval : DEFAULT_INTERVAL_SECONDS;
    m_deadline = now + (started.expires_in > 0 ? started.expires_in : DEFAULT_EXPIRES_IN_SECONDS);
}

Poller::Step Poller::on_reply(unsigned http_status, const std::string& body, std::time_t now)
{
    if (http_status >= 200 && http_status < 300) {
        std::optional<Credential> credential = parse_credential(body);
        if (!credential) {
            return {Step::Kind::Failed, 0, PairingFailure::ServerError, std::nullopt};
        }
        if (!has_operate_scope(*credential)) {
            return {Step::Kind::Failed, 0, PairingFailure::NoOperateScope, std::move(credential)};
        }
        return {Step::Kind::Succeeded, 0, PairingFailure::ServerError, std::move(credential)};
    }
    const std::string code = error_code(body);
    if (code == CODE_SLOW_DOWN) {
        m_interval += SLOW_DOWN_STEP_SECONDS;
        return poll_again(now);
    }
    const PairingFailure failure = failure_of_code(http_status, code);
    if (code == CODE_PENDING || failure == PairingFailure::Unreachable) {
        return poll_again(now);
    }
    return {Step::Kind::Failed, 0, failure, std::nullopt};
}

Poller::Step Poller::poll_again(std::time_t now) const
{
    if (now >= m_deadline) {
        return {Step::Kind::Failed, 0, PairingFailure::Expired, std::nullopt};
    }
    return {Step::Kind::PollIn, m_interval, PairingFailure::ServerError, std::nullopt};
}

} // namespace Slic3r::Biz::UserAccount::Pairing
