#include "Slic3r/App/Desktop/LeftBarCtrl.hpp"
#include "Slic3r/App/Desktop/TabsBarMenus.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include <Slic3r/App/WX/BitmapGetters.hpp>
#include <Slic3r/App/WX/StringConversions.hpp>

#include "Slic3r/App/WX/I18N.hpp"

#include <wx/sizer.h>

namespace Slic3r::App::Desktop {

using namespace Slic3r::App::WX;

LeftBarCtrl::LeftBarCtrl(wxWindow* parent, int orient, TabsBarMenus* menus)
: TabsBarCtrl(parent, orient, menus)
{
    m_login_icon_sz = compact_mode() ? 32 : 42;
    auto add_btn = [this](Button* btn) -> void {
        m_second_sizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, m_btn_margin);
        btn->set_margin(w_config()->em_unit(this));
    };

    m_preferences_btn = new Button(this, { wxEmptyString, "cog_wx", m_action_btn_sz, wxVERTICAL });
    m_preferences_btn->SetToolTip(from_u8(Biz::_u8L("Preferences")));
    add_btn(m_preferences_btn);

    m_account_btn = new ButtonWithPopup(this, "user", orient, m_login_icon_sz);
    add_btn(m_account_btn);
    
    m_account_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        m_account_btn->set_selected(true);
        m_menus->Popup(this, &m_menus->account, m_account_btn->get_popup_pos());
    });

    m_account_btn->set_compact_mode(compact_mode(), m_login_icon_sz);
    m_preferences_btn->set_compact_mode(compact_mode(), m_action_btn_sz, false);

    ShowUserAccount(AppServices::instance().app_config().is_prusa_account_enabled());
}

void LeftBarCtrl::OnColorsChanged()
{
    TabsBarCtrl::OnColorsChanged();

    m_account_btn->sys_color_changed();
    m_preferences_btn->sys_color_changed();

    UpdateAccountButton(true);
}

void LeftBarCtrl::UpdateAccountButton(bool avatar/* = false*/)
{
    if (!m_menus)
        return;

    TabsBarMenus::UserAccountInfo  user_account = m_menus->get_user_account_info();
    const wxString user_name = user_account.is_logged ? from_u8(user_account.user_name) : _L("Sign in");
    m_account_btn->SetToolTip(user_name);
    if (avatar) {
        if (user_account.is_logged) {
            // Icons obtained from avatar_path are expected to be PNG, JPG, or JPEG files, and not SVG.
            ScalableBitmap new_logo = ScalableBitmap::create_from_png_or_jpg(
                this,
                user_account.avatar_path,
                wxSize(m_login_icon_sz, m_login_icon_sz),
                true
            );
            if (new_logo.IsOk())
                m_account_btn->set_bitmap_bundle(new_logo.bmp());
            else
                m_account_btn->set_bitmap_bundle(*get_bmp_bundle("user", m_login_icon_sz));
        }
        else {
            m_account_btn->set_bitmap_bundle(*get_bmp_bundle("user", m_login_icon_sz));
        }
        m_account_btn->Refresh();
    }

    this->Layout();
}

void LeftBarCtrl::UnselectPopupButtons()
{
    m_account_btn->set_selected(false);
}

void LeftBarCtrl::on_rescale()
{
    int margin = w_config()->em_unit(this);
    m_account_btn->set_margin(margin);
    m_preferences_btn->set_margin(margin);
}

TabsBarCtrl::ButtonWithPopup* LeftBarCtrl::account_btn() const
{
    return m_account_btn;
}

TabsBarCtrl::Button* LeftBarCtrl::preferences_btn() const
{
    return m_preferences_btn;
}

void LeftBarCtrl::on_compact_mode_changed()
{
    const bool compact = compact_mode();
    m_login_icon_sz = compact ? 32 : 42;
    UpdateAccountButton(true);
    m_account_btn->set_compact_mode(compact, m_login_icon_sz, false);
    m_preferences_btn->set_compact_mode(compact, m_action_btn_sz, false);
    for (int sizer_item_id = 0; sizer_item_id < m_second_sizer->GetItemCount(); sizer_item_id++) {
        m_second_sizer->GetItem(sizer_item_id)
            ->SetFlag(wxALIGN_CENTER_HORIZONTAL | (m_compact_mode ? wxTOP : wxALL));
    }
    m_second_sizer->Layout();
    Refresh();
}

void LeftBarCtrl::ShowUserAccount(bool show)
{
    m_account_btn->Show(show);
    wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_TABS_BAR_FORCE_FULL_LAYOUT);
    wxPostEvent(GetParent(), evt);
}

} // namespace Slic3r::App::Desktop
