#pragma once

#include "TabsBarCtrl.hpp"

namespace Slic3r::App::Desktop {

class LeftBarCtrl : public TabsBarCtrl
{
public:
    LeftBarCtrl(wxWindow* parent, int orient, TabsBarMenus* menus = nullptr);
    ~LeftBarCtrl() = default;

    void OnColorsChanged() override;
    void UnselectPopupButtons() override;

    void UpdateAccountButton(bool avatar /* = false*/);
    void ShowUserAccount(bool show);

    Button* preferences_btn() const;
    ButtonWithPopup* account_btn() const;

protected:
    void on_compact_mode_changed() override;
    void on_rescale() override;

private:
    Button* m_preferences_btn{nullptr};
    ButtonWithPopup* m_account_btn{nullptr};

    int m_login_icon_sz{42};
    int m_action_btn_sz{20};
};

} // namespace Slic3r::App::Desktop
