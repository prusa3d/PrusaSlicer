#pragma once

#include "Slic3r/Biz/UserAccount/IUserAccountActionCallbacks.hpp"

#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Connect Local app pairing (offline sign-in), without any I/O: request bodies, parsing of
 * the server replies and the state of a running pairing. UserAccountCommunicationPairing does the
 * requests and the timing.
 *
 * The flow: POST app-pairings returns a device code (secret, for polling) and a user code (shown
 * to the person). The person signs in on the server's /pair page and approves the code; meanwhile
 * the app polls app-pairings/token with the device code until it gets the credential.
 */
namespace Slic3r::Biz::UserAccount::Pairing {

constexpr std::string_view APP_NAME      = "PrusaSlicer";
constexpr std::string_view OPERATE_SCOPE = "operate";

/** @brief Default poll interval when the server does not say, and the step of a slow_down. */
constexpr int DEFAULT_INTERVAL_SECONDS = 5;
constexpr int SLOW_DOWN_STEP_SECONDS   = 5;

/** @brief A pairing the server started. */
struct Started
{
    std::string device_code;
    std::string user_code;
    int expires_in{0};
    int interval{DEFAULT_INTERVAL_SECONDS};
};

/** @brief The credential a pairing ends with. */
struct Credential
{
    std::string id;
    /** @brief The JWT shaped access_token, used as the bearer everywhere. */
    std::string token;
    /** @brief Unix time, 0 when unknown. */
    std::time_t expires_at{0};
    /** @brief nullopt when the reply does not list them. */
    std::optional<std::vector<std::string>> scopes;
};

/** @brief Body of the request that starts a pairing. */
std::string start_request_body();

/** @brief Body of the request that collects the credential. */
std::string poll_request_body(const std::string& device_code);

/** @brief nullopt when the reply misses the device or user code. */
std::optional<Started> parse_started(const std::string& body);

/** @brief nullopt when the reply misses the id or the access_token. */
std::optional<Credential> parse_credential(const std::string& body);

/** @brief The `code` of an error reply of the core API, empty when there is none. */
std::string error_code(const std::string& body);

/**
 * @brief Why a pairing request failed.
 * @param http_status The error status, 0 when no response came.
 */
PairingFailure failure_of(unsigned http_status, const std::string& body);

/** @brief What an error reply of the page session endpoint (app-pairings/session) means. */
enum class PageSessionError
{
    /** @brief 401: the credential is refused, the account logs out. */
    Unauthorized,
    /** @brief 403: the server gives this credential no page session. */
    Refused,
    /** @brief 429 */
    Limited,
    /** @brief No answer or anything else. */
    Failed,
};

PageSessionError classify_page_session_error(unsigned http_status, const std::string& body);

/**
 * @brief State of one running pairing: the poll interval (raised by slow_down) and the deadline.
 * Times are unix seconds given by the caller, so the logic can be tested.
 */
class Poller
{
public:
    /** @brief What to do after a poll reply. */
    struct Step
    {
        enum class Kind
        {
            /** @brief Poll again after `seconds`. */
            PollIn,
            /** @brief The credential is approved and usable. */
            Succeeded,
            /**
             * @brief The pairing is over, see `failure`. A credential is set if one was issued and
             * has to be revoked (it lacks the operate scope).
             */
            Failed,
        };
        Kind kind{Kind::Failed};
        int seconds{0};
        PairingFailure failure{PairingFailure::ServerError};
        std::optional<Credential> credential;
    };

    void start(const Started& started, std::time_t now);

    const Started& started() const
    {
        return m_started;
    }

    int interval() const
    {
        return m_interval;
    }

    /**
     * @param http_status 2xx for an approval, the error status otherwise (0 when no response).
     */
    Step on_reply(unsigned http_status, const std::string& body, std::time_t now);

private:
    Step poll_again(std::time_t now) const;

    Started m_started;
    int m_interval{DEFAULT_INTERVAL_SECONDS};
    std::time_t m_deadline{0};
};

} // namespace Slic3r::Biz::UserAccount::Pairing
