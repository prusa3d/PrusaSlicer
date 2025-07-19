#ifndef slic3r_TopBarMenus_hpp_
#define slic3r_TopBarMenus_hpp_

#include <wx/menu.h>
#include <boost/filesystem.hpp>

class TopBarItemsCtrl;
class wxString;

class TopBarMenus
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
    wxMenuItem*             m_hide_login_item   { nullptr };

    TopBarItemsCtrl*        m_popup_ctrl        { nullptr };

    std::function<int()>            m_cb_get_mode                   { nullptr };
    std::function<void(int)>        m_cb_save_mode                  { nullptr };
    std::function<std::string(int)> m_cb_get_mode_btn_color         { nullptr };

    std::function<void()>           m_cb_act_with_user_account      { nullptr };
    std::function<void()>           m_cb_hide_user_account          { nullptr };
    std::function<UserAccountInfo()>m_cb_get_user_account_info      { nullptr };

public:
    wxMenu          main;
    wxMenu          workspaces;
    wxMenu          account;

    TopBarMenus();
    ~TopBarMenus() = default;

    void AppendMenuItem(wxMenu* menu, const wxString& title);
    void AppendMenuSeparaorItem();
    void ApplyWorkspacesMenu();
    void CreateAccountMenu();
    void UpdateAccountMenu();
    void RemoveHideLoginItem();

    void Popup(TopBarItemsCtrl* popup_ctrl, wxMenu* menu, wxPoint pos);
    void BindEvtClose();

    void        sys_color_changed();

    wxString            get_workspace_name(/*ConfigOptionMode*/int mode = -1);
    wxBitmapBundle*     get_workspace_bitmap(/*ConfigOptionMode*/int mode = -1);

    UserAccountInfo     get_user_account_info();

    void set_workspaces_menu_callbacks(std::function</*ConfigOptionMode*/int()>             cb_get_mode,
                                       std::function<void(/*ConfigOptionMode*/int)>         cb_save_mode,
                                       std::function<std::string(/*ConfigOptionMode*/int)>  cb_get_mode_btn_color)
    {
        m_cb_get_mode           = cb_get_mode;
        m_cb_save_mode          = cb_save_mode;
        m_cb_get_mode_btn_color = cb_get_mode_btn_color;

        ApplyWorkspacesMenu();
    }

    void set_account_menu_callbacks(std::function<void()>               cb_act_with_user_account  ,
                                    std::function<void()>               cb_hide_user_account      ,
                                    std::function<UserAccountInfo()>    cb_get_user_account_info   )
    {
        m_cb_act_with_user_account   = cb_act_with_user_account;
        m_cb_hide_user_account       = cb_hide_user_account;
        m_cb_get_user_account_info   = cb_get_user_account_info;
    }

};

#endif // slic3r_TopBarMenus_hpp_
