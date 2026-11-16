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

    const auto choose = [this](AccountState state)
    {
        return [this, state](wxCommandEvent&)
        {
            if (m_cb_choose_account_state) {
                m_cb_choose_account_state(state);
            }
        };
    };
    m_signed_out_item = append_menu_radio_item(&account_states, wxID_ANY, _L("Signed out"), {},
        choose(AccountState::SignedOut), nullptr);
    m_prusa_account_item = append_menu_radio_item(&account_states, wxID_ANY, _L("Prusa Account"), {},
        choose(AccountState::PrusaAccount), nullptr);
    m_connect_local_item = append_menu_radio_item(&account_states, wxID_ANY, _L("Prusa Connect on-premise"), {},
        choose(AccountState::ConnectLocal), nullptr);
}

void TabsBarMenus::UpdateAccountMenu()
{
    const UserAccountInfo info = get_user_account_info();
    if (m_login_item) {
        m_login_item->SetItemLabel(info.is_logged ? _L("Sign out") : _L("Sign in"));
        set_menu_item_bitmap(m_login_item, info.is_logged ? "logout" : "login");
    }
    if (m_signed_out_item && m_prusa_account_item && m_connect_local_item) {
        m_signed_out_item->Check(!info.is_logged);
        m_prusa_account_item->Check(info.is_logged && !info.connect_local);
        m_connect_local_item->Check(info.is_logged && info.connect_local);
    }
}

wxMenu* TabsBarMenus::AccountMenu()
{
    return get_user_account_info().both_services ? &account_states : &account;
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
    account_states.Bind(wxEVT_MENU_CLOSE, [close_fn](wxMenuEvent&) { close_fn(); });
}

void TabsBarMenus::set_account_menu_callbacks(
    std::function<void()> cb_act_with_user_account,
    std::function<UserAccountInfo()> cb_get_user_account_info,
    std::function<void(AccountState)> cb_choose_account_state
)
{
    m_cb_act_with_user_account = cb_act_with_user_account;
    m_cb_get_user_account_info = cb_get_user_account_info;
    m_cb_choose_account_state  = cb_choose_account_state;
}

} // Slic3r::App::Desktop