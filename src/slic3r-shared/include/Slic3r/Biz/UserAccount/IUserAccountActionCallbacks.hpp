#pragma once

#include "Slic3r/Biz/Network/IHttp.hpp"

namespace Slic3r::Biz::UserAccount {

enum class ActionSuccessType
{
    None,
    UserID,
    UserIDAfterToken,
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
 * @brief Why a Connect Local app pairing (offline sign-in) ended without a credential.
 */
enum class PairingFailure
{
    /** @brief The person denied the request on the server. */
    Denied,
    /** @brief Nobody approved it in time. */
    Expired,
    /** @brief The server does not know the pairing (anymore), e.g. it was restarted. */
    Unknown,
    /** @brief Too many pairings were started from this address recently. */
    Limited,
    /** @brief The approved credential does not allow printing. */
    NoOperateScope,
    /** @brief The server cannot be reached. */
    Unreachable,
    /** @brief Any other unexpected reply. */
    ServerError,
    /** @brief The credential was approved, but refused or dropped before the account was loaded. */
    CredentialLost,
    /**
     * @brief The credential was approved, but the server has no account (public username) for it.
     */
    NoAccount,
    /**
     * @brief The credential was approved and is kept, but the account cannot be loaded yet (server
     * unreachable or failing). Signing in finishes in the background once it can.
     */
    AccountUnavailable,
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
