#pragma once

#include "Slic3r/Biz/UserAccount/UserAccountActionData.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountAction.hpp"
#include "Slic3r/Biz/Network/ServiceConfig.hpp"
#include "Slic3r/Assert.hpp"

#include <map>

namespace Slic3r::Biz::UserAccount {

/**
 * @brief Database of all Actions. All Actions live here as unique ptr, possibly reused indefinitely.
 */
class UserAccountActionStore {
public:
	UserAccountActionStore()
	{
        Network::ServiceConfig& sc = Network::ServiceConfig::instance();
		m_actions[UserAccountActionID::Dummy] = std::make_unique<UserAccountActionDummy>();
        m_actions[UserAccountActionID::RefreshToken] = std::make_unique<UserAccountActionPost>("EXCHANGE_TOKENS", sc.account_token_url());
        m_actions[UserAccountActionID::CodeForToken] = std::make_unique<UserAccountActionPost>("EXCHANGE_TOKENS", sc.account_token_url());
        m_actions[UserAccountActionID::PrintablesSecretToken] = std::make_unique<UserAccountActionPost>("PRINTABLES_SECRET_TOKEN", sc.printables_get_secret_token_url());
        m_actions[UserAccountActionID::UserId] = std::make_unique<UserAccountActionGetWithEvent>("USER_ID", sc.account_me_url(), ActionSuccessType::UserID, ActionFailType::Reset);
        m_actions[UserAccountActionID::UserIdAfterTokenSuccess] = std::make_unique<UserAccountActionGetWithEvent>("USER_ID_AFTER_TOKEN_SUCCESS", sc.account_me_url(), ActionSuccessType::UserIDAfterToken, ActionFailType::Reset);
        m_actions[UserAccountActionID::TestAccessToken] = std::make_unique<UserAccountActionGetWithEvent>("TEST_ACCESS_TOKEN", sc.account_me_url(), ActionSuccessType::UserID, ActionFailType::Fail);
        //m_actions[UserAccountActionID::TestConnection] = std::make_unique<UserAccountActionGetWithEvent>("TEST_CONNECTION", sc.account_me_url(), ActionSuccessType::None, ActionFailType::Reset);
        m_actions[UserAccountActionID::Avatar] = std::make_unique<UserAccountActionGetWithEvent>("AVATAR", std::string(), ActionSuccessType::Avatar, ActionFailType::Fail, false);
        m_actions[UserAccountActionID::ConnectStatus] = std::make_unique<UserAccountActionGetWithEvent>("CONNECT_STATUS", sc.connect_status_url(), ActionSuccessType::ConnectStatus, ActionFailType::Fail);
        m_actions[UserAccountActionID::ConnectPrinterModels] = std::make_unique<UserAccountActionGetWithEvent>("CONNECT_PRINTER_MODELS", sc.connect_printer_list_url(), ActionSuccessType::ConnectPrinterModels, ActionFailType::Fail);
        m_actions[UserAccountActionID::ConnectDataFromUuid] = std::make_unique<UserAccountActionGetWithEvent>("CONNECT_DATA_FROM_UUID", sc.connect_printers_url(), ActionSuccessType::PrinterData, ActionFailType::PrinterData);
	}
	~UserAccountActionStore() = default;

	UserAccountActionStore(const UserAccountActionStore& ) = delete;
    UserAccountActionStore(UserAccountActionStore&& other) = delete;
    UserAccountActionStore& operator=(const UserAccountActionStore& ) = delete;
    UserAccountActionStore& operator=(UserAccountActionStore&& other) = delete;

    IUserAccountAction* operator[](UserAccountActionID actionID) {
        ASSERT(m_actions.find(actionID) != m_actions.end());
        return m_actions[actionID].get();
    }

private:
	 std::map<UserAccountActionID, std::unique_ptr<IUserAccountAction>>     m_actions;
};
}