#pragma once

#include "Slic3r/Biz/Network/IHttp.hpp"

namespace Slic3r::Biz::UserAccount {

enum class ActionSuccessType
{
    None,
    UserID,
    UserIDAfterToken,
    ConnectStatus,
    ConnectPrinterModels,
    Avatar,
    PrinterData,
};
enum class ActionFailType
{
    None,
    Fail,
    Reset,
    PrinterData, // Fail level
};

/**
 * @brief Interface for UserAccount action callbacks.
 ** This interface is used to notify the UserAccountSessionDispatchBase about the results of actions.
 */
class IUserAccountActionCallbacks
{
public:
    virtual ~IUserAccountActionCallbacks() = default;

    virtual void on_action_retry(const Network::IHttp::Retry& retry)                 = 0;
    virtual void on_action_success(ActionSuccessType success_type, std::string body) = 0;
    virtual void on_action_fail(ActionFailType fail_type, std::string body)          = 0;
};
} // namespace Slic3r::Biz::UserAccount
