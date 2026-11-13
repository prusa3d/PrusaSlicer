#pragma once

#include <vector>
#include <string>
#include <functional>

namespace Slic3r::Biz::UserAccount {

typedef std::function<void(const std::string& body)> ActionSuccessFn;
/**
 * @brief Called when the request failed, with the HTTP status (0 when no response came, e.g. the
 * server is unreachable or the request was cancelled).
 */
typedef std::function<void(const std::string& body, unsigned status)> ActionFailFn;

enum class UserAccountActionID {
    Dummy,
    RefreshToken,
    CodeForToken,
    UserId,
    UserIdAfterTokenSuccess,
    TestAccessToken,
    TestConnection,
    ConnectPrinterModels, // status of all printers by UUID with printer_model. Should be called once to save printer models.
    Avatar,
    ConnectDataFromUuid,
    PrintablesSecretToken,
    // Connect Local (offline mode): app pairing and the page session of its web pages.
    PairingRequest,
    PairingPoll,
    PageSession
};

struct ActionQueueData
{
    UserAccountActionID action_id;
    ActionSuccessFn     success_callback;
    ActionFailFn        fail_callback;
    std::string         input;
    std::vector<std::pair<std::string, std::string>> additional_headers;
    /**
     * @brief False: a failure is reported at once, without retries, and the owner decides what
     * next. A POST then sends no default Content-Type, only additional_headers.
     */
    bool retry{true};
};

}