#pragma once

#include <vector>
#include <string>
#include <functional>

namespace Slic3r::Biz::UserAccount {

typedef std::function<void(const std::string& body)> ActionSuccessFn;
typedef std::function<void(const std::string& body)> ActionFailFn;

enum class UserAccountActionID {
    Dummy,
    RefreshToken,
    CodeForToken,
    UserId,
    UserIdAfterTokenSuccess,
    TestAccessToken,
    TestConnection,
    ConnectStatus, // status of all printers by UUID
    ConnectPrinterModels, // status of all printers by UUID with printer_model. Should be called once to save printer models.
    Avatar,
    ConnectDataFromUuid,
    PrintablesSecretToken,
    ConnectPrinterStates
};

struct ActionQueueData
{
    UserAccountActionID action_id;
    ActionSuccessFn     success_callback;
    ActionFailFn        fail_callback;
    std::string         input;
    std::vector<std::pair<std::string, std::string>> additional_headers;
};

}