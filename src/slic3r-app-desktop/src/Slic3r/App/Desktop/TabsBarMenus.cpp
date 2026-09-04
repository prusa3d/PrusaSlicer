#include "Slic3r/App/Desktop/TabsBarMenus.hpp"
#include "Slic3r/App/Desktop/TabsBarCtrl.hpp"

#include "Slic3r/App/WX/MenuManaging.hpp"
#include "Slic3r/App/WX/I18N.hpp"
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include <Slic3r/Assert.hpp>

//#include "GUI_App.hpp"
// #include "GUI_Factories.hpp"

#include "Slic3r/App/WX/StringConversions.hpp"

namespace Slic3r::App::Desktop {

using namespace Slic3r::App::WX;

TabsBarMenus::TabsBarMenus()
{
    CreateAccountMenu();
    UpdateAccountMenu();

    BindEvtClose();
}

TabsBarMenus::UserAccountInfo TabsBarMenus::get_user_account_info()
{
    if (m_cb_get_user_account_info)
        return m_cb_get_user_account_info();
    return UserAccountInfo();
}

void TabsBarMenus::CreateAccountMenu()
{
    m_login_item = append_menu_item(&account, wxID_ANY, from_u8("Sign in"), {},
        [this](wxCommandEvent&) { if (m_cb_act_with_user_account) m_cb_act_with_user_account(); }, "login");
}

void TabsBarMenus::UpdateAccountMenu()
{
    bool is_logged{ false };
    if (m_cb_get_user_account_info)
        is_logged = m_cb_get_user_account_info().is_logged;
    if (m_login_item) {
        m_login_item->SetItemLabel(is_logged ? _L("Sign out") : _L("Sign in"));
        set_menu_item_bitmap(m_login_item, is_logged ? "logout" : "login");
    }
}

void TabsBarMenus::Popup(TabsBarCtrl* popup_ctrl, wxMenu* menu, wxPoint pos)
{
    m_popup_ctrl = popup_ctrl;
    m_popup_ctrl->PopupMenu(menu, pos);
}

void TabsBarMenus::BindEvtClose()
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

void TabsBarMenus::set_account_menu_callbacks(
    std::function<void()> cb_act_with_user_account,
    std::function<UserAccountInfo()> cb_get_user_account_info
)
{
    m_cb_act_with_user_account = cb_act_with_user_account;
    m_cb_get_user_account_info = cb_get_user_account_info;
}

} // Slic3r::App::Desktop