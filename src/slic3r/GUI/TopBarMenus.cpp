#include "TopBarMenus.hpp"
#include "TopBar.hpp"

#include "libslic3r/Config.hpp" //ConfigOptionMode
#include "GUI_Factories.hpp"
#include "I18N.hpp"

using namespace Slic3r::GUI;

TopBarMenus::TopBarMenus()
{
    CreateAccountMenu();
    UpdateAccountMenu();

    BindEvtClose();
}

void TopBarMenus::AppendMenuItem(wxMenu* menu, const wxString& title)
{
    append_submenu(&main, menu, wxID_ANY, title, "cog");
}

void TopBarMenus::AppendMenuSeparaorItem()
{
    main.AppendSeparator();
}

wxString TopBarMenus::get_workspace_name(int mode/* = -1*/)
{
    if (mode < 0 && m_cb_get_mode)
        mode = m_cb_get_mode();

    return  mode == Slic3r::ConfigOptionMode::comSimple   ? _L("Beginner mode") :
            mode == Slic3r::ConfigOptionMode::comAdvanced ? _L("Normal mode")  : _L("Expert mode");
}

wxBitmapBundle* TopBarMenus::get_workspace_bitmap(int mode/* = -1*/)
{
    assert(m_cb_get_mode_btn_color);
    if (mode < 0 && m_cb_get_mode)
        mode = m_cb_get_mode();

    return get_bmp_bundle("mode", 16, -1, m_cb_get_mode_btn_color(mode));
}

TopBarMenus::UserAccountInfo TopBarMenus::get_user_account_info()
{
    if (m_cb_get_user_account_info)
        return m_cb_get_user_account_info();
    return UserAccountInfo();
}

void TopBarMenus::sys_color_changed()
{
    MenuFactory::sys_color_changed(&main);
    MenuFactory::sys_color_changed(&workspaces);
    MenuFactory::sys_color_changed(&account);
}

void TopBarMenus::ApplyWorkspacesMenu()
{
    wxMenuItemList& items = workspaces.GetMenuItems();
    if (!items.IsEmpty()) {
        for (int id = int(workspaces.GetMenuItemCount()) - 1; id >= 0; id--)
            workspaces.Destroy(items[id]);
    }

    for (const Slic3r::ConfigOptionMode& mode : { Slic3r::ConfigOptionMode::comSimple,
                                                  Slic3r::ConfigOptionMode::comAdvanced,
                                                  Slic3r::ConfigOptionMode::comExpert }) {
        const wxString label = get_workspace_name(mode);
        append_menu_item(&workspaces, wxID_ANY, label, label,
            [mode, this](wxCommandEvent&) {
                if (m_cb_get_mode && m_cb_save_mode &&
                    m_cb_get_mode() != mode)
                    m_cb_save_mode(mode);
            }, get_workspace_bitmap(mode));

        if (mode < Slic3r::ConfigOptionMode::comExpert)
            workspaces.AppendSeparator();
    }
}

void TopBarMenus::CreateAccountMenu()
{
    m_login_item = append_menu_item(&account, wxID_ANY, "", "",
        [this](wxCommandEvent&) { if (m_cb_act_with_user_account) m_cb_act_with_user_account(); }, "login");

    m_hide_login_item = append_menu_item(&account, wxID_ANY, _L("Hide \"Log in\" button"), "",
        [this](wxCommandEvent&) { if (m_cb_hide_user_account) m_cb_hide_user_account(); });
}

void TopBarMenus::UpdateAccountMenu()
{
    bool is_logged{ false };
    if (m_cb_get_user_account_info)
        is_logged = m_cb_get_user_account_info().is_logged;
    if (is_logged)
        RemoveHideLoginItem();
    if (m_login_item) {
        m_login_item->SetItemLabel(is_logged ? _L("Log out") : _L("Log in"));
        set_menu_item_bitmap(m_login_item, is_logged ? "logout" : "login");
    }
}

void TopBarMenus::RemoveHideLoginItem()
{
    if (m_hide_login_item)
        account.Destroy(m_hide_login_item);
}

void TopBarMenus::Popup(TopBarItemsCtrl* popup_ctrl, wxMenu* menu, wxPoint pos)
{
    m_popup_ctrl = popup_ctrl;
    m_popup_ctrl->PopupMenu(menu, pos);
}

void TopBarMenus::BindEvtClose()
{
    auto close_fn = [this]() {
        if (m_popup_ctrl)
            m_popup_ctrl->UnselectPopupButtons();
        m_popup_ctrl = nullptr;
    };

    main.        Bind(wxEVT_MENU_CLOSE, [close_fn](wxMenuEvent&) { close_fn(); });
    workspaces.  Bind(wxEVT_MENU_CLOSE, [close_fn](wxMenuEvent&) { close_fn(); });
    account.     Bind(wxEVT_MENU_CLOSE, [close_fn](wxMenuEvent&) { close_fn(); });
}