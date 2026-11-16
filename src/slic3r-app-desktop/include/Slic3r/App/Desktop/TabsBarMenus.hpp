#pragma once

#include <wx/menu.h>
#include <boost/filesystem.hpp>

class wxString;

namespace Slic3r::App::Desktop {

class TabsBarCtrl;

class TabsBarMenus
{
public:
    struct UserAccountInfo {
        bool                    is_logged       { false };
        std::string             user_name;
        boost::filesystem::path avatar_path;
        bool                    connect_local      { false };
        bool                    server_set      { true };
        bool                    both_services   { false };
    };

    enum class AccountState { SignedOut, PrusaAccount, ConnectLocal };

private:

    // Prusa Account menu items
    wxMenuItem*             m_login_item        { nullptr };
    wxMenuItem*             m_signed_out_item   { nullptr };
    wxMenuItem*             m_prusa_account_item{ nullptr };
    wxMenuItem*             m_connect_local_item   { nullptr };
    TabsBarCtrl*            m_popup_ctrl        { nullptr };

    std::function<int()>            m_cb_get_mode                   { nullptr };
    std::function<void(int)>        m_cb_save_mode                  { nullptr };
    std::function<std::string(int)> m_cb_get_mode_btn_color         { nullptr };

    std::function<void()>           m_cb_act_with_user_account      { nullptr };
    std::function<UserAccountInfo()>m_cb_get_user_account_info      { nullptr };
    std::function<void(AccountState)> m_cb_choose_account_state     { nullptr };

public:
    wxMenu          main;
    wxMenu          workspaces;
    wxMenu          account;
    wxMenu          account_states;

    TabsBarMenus();
    ~TabsBarMenus() = default;

    void CreateAccountMenu();
    void UpdateAccountMenu();

    /**
     * @brief The menu of the account button: the three states while both account services are
     * enabled, else the one with sign in / sign out.
     */
    wxMenu* AccountMenu();

    void Popup(TabsBarCtrl* popup_ctrl, wxMenu* menu, wxPoint pos);
    void BindEvtClose();

    UserAccountInfo     get_user_account_info();

    void set_account_menu_callbacks(
        std::function<void()> cb_act_with_user_account,
        std::function<UserAccountInfo()> cb_get_user_account_info,
        std::function<void(AccountState)> cb_choose_account_state
    );
};

} // namespace Slic3r::App::Desktop
