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
    };

private:

    // Prusa Account menu items
    wxMenuItem*             m_login_item        { nullptr };
    TabsBarCtrl*            m_popup_ctrl        { nullptr };

    std::function<int()>            m_cb_get_mode                   { nullptr };
    std::function<void(int)>        m_cb_save_mode                  { nullptr };
    std::function<std::string(int)> m_cb_get_mode_btn_color         { nullptr };

    std::function<void()>           m_cb_act_with_user_account      { nullptr };
    std::function<UserAccountInfo()>m_cb_get_user_account_info      { nullptr };

public:
    wxMenu          main;
    wxMenu          workspaces;
    wxMenu          account;

    TabsBarMenus();
    ~TabsBarMenus() = default;

    void CreateAccountMenu();
    void UpdateAccountMenu();

    void Popup(TabsBarCtrl* popup_ctrl, wxMenu* menu, wxPoint pos);
    void BindEvtClose();

    UserAccountInfo     get_user_account_info();

    void set_account_menu_callbacks(
        std::function<void()> cb_act_with_user_account,
        std::function<UserAccountInfo()> cb_get_user_account_info
    );
};

} // namespace Slic3r::App::Desktop
