#include "TopBar.hpp"
#include "TopBarMenus.hpp"

#include "GUI_App.hpp"
#include "Search.hpp"
#include "format.hpp"
#include "I18N.hpp"

#include <wx/button.h>
#include <wx/sizer.h>

wxDEFINE_EVENT(wxCUSTOMEVT_TOPBAR_SEL_CHANGED, wxCommandEvent);

using namespace Slic3r::GUI;

TopBarItemsCtrl::Button::Button(wxWindow* parent, const wxString& label, const std::string& icon_name, const int px_cnt, wxSize size_def)
:wxPanel(parent, wxID_ANY, wxDefaultPosition, size_def, wxBORDER_NONE | wxTAB_TRAVERSAL)
#ifdef _WIN32
,m_background_color(wxGetApp().get_window_default_clr())
#else
,m_background_color(wxTransparentColor)
#endif
,m_foreground_color(wxGetApp().get_label_clr_default())
,m_bmp_bundle(icon_name.empty() ? wxBitmapBundle() : *get_bmp_bundle(icon_name, px_cnt))
,m_label(label)
,m_icon_name(icon_name)
,m_px_cnt(px_cnt)
,m_has_down_arrow(!icon_name.empty())
,m_dd_bmp_bundle(m_has_down_arrow ? *get_bmp_bundle("drop_down") : wxBitmapBundle())
{
    int btn_margin = em_unit(this);
    int x, y;
    GetTextExtent(label.IsEmpty() ? "a" : label, &x, &y);
    wxSize size(x + 4 * btn_margin, y + int(1.5 * btn_margin));
    if (icon_name.empty())
        this->SetMinSize(size);
    else if (label.IsEmpty()) {
        const int btn_side = size.y;
        this->SetMinSize(wxSize(btn_side, btn_side));
    }
    else
#ifdef _WIN32
        this->SetMinSize(wxSize(-1, size.y));
#else
        this->SetMinSize(wxSize(size.x + px_cnt, size.y));
#endif

    //button events
    Bind(wxEVT_SET_FOCUS,    [this](wxFocusEvent& event) { set_hovered(true ); event.Skip(); });
    Bind(wxEVT_KILL_FOCUS,   [this](wxFocusEvent& event) { set_hovered(false); event.Skip(); });
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event) { set_hovered(true ); event.Skip(); });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) { set_hovered(false); event.Skip(); });

    Bind(wxEVT_PAINT,        [this](wxPaintEvent&) { render(); });
#ifdef __linux__
    Bind(wxEVT_LEFT_UP,      [this](wxMouseEvent& event) {
#else
    Bind(wxEVT_LEFT_DOWN,    [this](wxMouseEvent& event) {
#endif
        wxCommandEvent evt(wxEVT_BUTTON, GetId());
        GetEventHandler()->AddPendingEvent(evt);
        event.Skip();
    });
}

void TopBarItemsCtrl::Button::set_selected(bool selected)
{
    m_is_selected = selected;

    m_foreground_color = m_is_selected ? wxGetApp().get_window_default_clr(): wxGetApp().get_label_clr_default() ;
    m_background_color = m_is_selected ? wxGetApp().get_label_clr_default() : 
#ifdef _WIN32
                                         wxGetApp().get_window_default_clr();
#else
                                         wxTransparentColor;
#endif
}

void TopBarItemsCtrl::Button::set_hovered(bool hovered)
{
    using namespace Slic3r::GUI;

#ifdef _WIN32
    this->GetParent()->Refresh(); // force redraw a background of the selected mode button
#endif /* no _WIN32 */

    m_background_color =    m_is_selected ? wxGetApp().get_label_clr_default()     :
                            hovered       ? wxGetApp().get_color_selected_btn_bg() :
                            
#ifdef _WIN32
                                            wxGetApp().get_window_default_clr();
#else
                                            wxTransparentColor;
#endif

    this->Refresh();
    this->Update();
}

void TopBarItemsCtrl::Button::render()
{
    const wxRect rc(GetSize());
    wxPaintDC dc(this);

    int em = em_unit(this);

    // Draw def rect with rounded corners

    dc.SetPen(m_background_color);
    dc.SetBrush(m_background_color);
    dc.DrawRoundedRectangle(rc, int(0.4* em));

    wxPoint pt = { 0, 0 };

    wxString text = m_label;

    if (m_bmp_bundle.IsOk()) {
        wxSize szIcon = get_preferred_size(m_bmp_bundle, this);
        pt.x = text.IsEmpty() ? ((rc.width - szIcon.x) / 2) : em;
        pt.y = (rc.height - szIcon.y) / 2;
        dc.DrawBitmap(m_bmp_bundle.GetBitmapFor(this), pt, true);
        pt.x += szIcon.x + int(0.5 * em);
    }

    // Draw text

    if (!text.IsEmpty()) {
        wxSize labelSize = dc.GetTextExtent(text);
        if (labelSize.x > rc.width)
            text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, rc.width);
        if (!m_bmp_bundle.IsOk())
            pt.x += (rc.width - pt.x - labelSize.x) / 2;
        pt.y = (rc.height - labelSize.y) / 2;

        dc.SetTextForeground(m_foreground_color);
        dc.SetFont(GetFont());
        dc.DrawText(text, pt);

        pt.x += labelSize.x + int(0.5 * em);

        // Draw down_arrow if needed

        if (m_dd_bmp_bundle.IsOk()) {
            wxSize szIcon = get_preferred_size(m_dd_bmp_bundle, this);
            pt.y = (rc.height - szIcon.y) / 2;
            dc.DrawBitmap(m_dd_bmp_bundle.GetBitmapFor(this), pt, true);
        }
    }
}

void TopBarItemsCtrl::Button::sys_color_changed()
{
    m_bmp_bundle = m_icon_name.empty() ? wxBitmapBundle() : *get_bmp_bundle(m_icon_name, m_px_cnt);

#ifdef _WIN32
    m_background_color = wxGetApp().get_window_default_clr();
#endif
    m_foreground_color = wxGetApp().get_label_clr_default();
}

const int login_icon_sz = 24;

TopBarItemsCtrl::ButtonWithPopup::ButtonWithPopup(wxWindow* parent, const wxString& label, const std::string& icon_name, const int px_cnt, wxSize size)
    :TopBarItemsCtrl::Button(parent, label, icon_name, px_cnt, size)
{
    if (size != wxDefaultSize)
        m_fixed_width = size.x * 0.1;

    this->SetLabel(label);
}

TopBarItemsCtrl::ButtonWithPopup::ButtonWithPopup(wxWindow* parent, const std::string& icon_name, int icon_width/* = 20*/, int icon_height/* = 20*/)
    :TopBarItemsCtrl::Button(parent, "", icon_name, icon_width)
{
}

void TopBarItemsCtrl::ButtonWithPopup::SetLabel(const wxString& label)
{
    wxString text = label;
    int btn_height = GetMinSize().GetHeight();

    if (label.IsEmpty()) {
        m_label = label;
        SetMinSize(wxSize(btn_height, btn_height));
        return;
    }

    const int em = em_unit(this);

    const int label_width   = GetTextExtent(text).GetWidth();
    int       width_margins = int(0.1 * em * (m_px_cnt + 16 + 25));

    this->SetMinSize(wxSize(label_width + width_margins, btn_height));

    if (m_fixed_width != wxDefaultCoord) {
        const int text_width = m_fixed_width * em_unit(this) - width_margins;
        if (label_width > text_width) {
            wxWindowDC wdc(this);
            text = wxControl::Ellipsize(text, wdc, wxELLIPSIZE_END, text_width);

            SetMinSize(wxSize(m_fixed_width * em_unit(this), btn_height));
            SetSize(wxSize(m_fixed_width * em_unit(this), btn_height));
        }
    }

    m_label = text;
    Refresh();
    GetParent()->Layout();
}

void TopBarItemsCtrl::UpdateAccountButton(bool avatar/* = false*/)
{
    TopBarMenus::UserAccountInfo  user_account = m_menus->get_user_account_info();
    const wxString user_name = user_account.is_logged ? from_u8(user_account.user_name) : _L("Log in");
    m_account_btn->SetToolTip(user_name);
    if (avatar) {
        if (user_account.is_logged) {
            ScalableBitmap new_logo(this, user_account.avatar_path, wxSize(login_icon_sz, login_icon_sz));
            if (new_logo.IsOk())
                m_account_btn->SetBitmapBundle(new_logo.bmp());
            else
                m_account_btn->SetBitmapBundle(*get_bmp_bundle("user", login_icon_sz));
        }
        else {
            m_account_btn->SetBitmapBundle(*get_bmp_bundle("user", login_icon_sz));
        }
    }

    m_account_btn->SetLabel(m_collapsed_btns ? "" : user_name);
    this->Layout();
}

void TopBarItemsCtrl::UnselectPopupButtons()
{
    if (m_menu_btn)
        m_menu_btn  ->set_selected(false);
    m_workspace_btn ->set_selected(false);
    m_account_btn   ->set_selected(false);
}

void TopBarItemsCtrl::CreateSearch()
{
    // Linux specific: If wxDefaultSize is used in constructor and than set just maxSize, 
    // than this max size will be used as a default control size and can't be resized.
    // So, set initial size for some minimum value
    m_search = new ::TextInput(this, wxGetApp().searcher().default_string, "", "search", wxDefaultPosition, wxSize(2 * em_unit(this), -1), wxTE_PROCESS_ENTER);
    m_search->SetMaxSize(wxSize(/*42*/30*em_unit(this), -1));
    wxGetApp().UpdateDarkUI(m_search);

    m_search->Bind(wxEVT_TEXT, [](wxEvent& e)
    {
        wxGetApp().searcher().edit_search_input();
        wxGetApp().update_search_lines();
    });

    m_search->Bind(wxEVT_MOVE, [](wxMoveEvent& event)
    { 
        event.Skip();
        wxGetApp().searcher().update_dialog_position();
    });

    m_search->SetOnDropDownIcon([this]() 
    {
        wxGetApp().searcher().set_search_input(m_search); 
        wxGetApp().show_search_dialog(); 
    });

    m_search->Bind(wxEVT_KILL_FOCUS, [](wxFocusEvent& e)
    {
        e.Skip();
        wxGetApp().searcher().check_and_hide_dialog();
    });

    wxTextCtrl* ctrl = m_search->GetTextCtrl();
    ctrl->SetToolTip(format_wxstr(_L("Search in settings [%1%]"), "Ctrl+F"));

    ctrl->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e)
    {
        wxGetApp().searcher().set_search_input(m_search); 
        if (e.GetKeyCode() == WXK_TAB)
            m_search->Navigate(e.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
        else
            wxGetApp().searcher().process_key_down_from_input(e);
        e.Skip();
    });

    ctrl->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event)
    {
        wxGetApp().searcher().set_search_input(m_search); 
        wxGetApp().show_search_dialog();
        event.Skip();
    });

    ctrl->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event)
    {
        if (m_search->GetValue() == wxGetApp().searcher().default_string)
            m_search->SetValue("");
        event.Skip();
    });
}

void TopBarItemsCtrl::UpdateSearchSizeAndPosition()
{
    if (!m_workspace_btn || !m_account_btn)
        return;

    int em = em_unit(this);

    wxWindow* parent_win = GetParent()->GetParent();
    int top_win_without_sidebar = parent_win->GetSize().GetWidth() - 42 * em;

    bool update_bnts{ false };
    if (top_win_without_sidebar - m_btns_width < 15 * em) {
        if (!m_collapsed_btns) {
            m_sizer->SetItemMinSize(1, wxSize(20, -1));
            m_collapsed_btns = update_bnts = true;
        }
    }
    else if (m_collapsed_btns) {
        m_sizer->SetItemMinSize(1, wxSize(42 * em, -1));
        m_collapsed_btns = false;
        update_bnts = true;
    }

    if (update_bnts) {
        UpdateMode();
        UpdateAccountButton();
    }
}

void TopBarItemsCtrl::UpdateSearch(const wxString& search)
{
    if (search != m_search->GetValue())
        m_search->SetValue(search);
}

void TopBarItemsCtrl::update_margins()
{
    int em = em_unit(this);
    m_btn_margin  = std::lround(0.5 * em);
}

wxPoint TopBarItemsCtrl::ButtonWithPopup::get_popup_pos()
{
    wxPoint pos = this->GetPosition();
    pos.y += this->GetSize().GetHeight() + int(0.2 * wxGetApp().em_unit());
    return pos;
}

void TopBarItemsCtrl::update_btns_width()
{
    int em = em_unit(this);

    m_btns_width = 2 * m_btn_margin;
    if (m_menu_btn)
        m_btns_width += m_menu_btn->GetSize().GetWidth();
    else
        m_btns_width += 4 * em;

    if (m_settings_btn)
        m_btns_width += m_settings_btn->GetSize().GetWidth() + m_btn_margin;
    else {
        for (const Button* btn : m_pageButtons)
            m_btns_width += btn->GetSize().GetWidth() + m_btn_margin;
    }

    // Check min width of parent and change it if needed

    int sizebar_w = 25;

    wxWindow* parent_win = GetParent()->GetParent();
    int top_win_without_sidebar = parent_win->GetSize().GetWidth() - sizebar_w * em;

    if (top_win_without_sidebar < 0)
        return;

    wxSize min_sz = parent_win->GetMinSize();
    if (m_btns_width < (76 - sizebar_w) * em) {
        if (min_sz.GetWidth() > 76 * em)
            parent_win->SetMinSize(wxSize(76 * em, 49 * em));
    }
    else {
        wxSize new_size = wxSize(m_btns_width + sizebar_w * em, 49 * em);
        parent_win->SetMinSize(new_size);
        if (top_win_without_sidebar < m_btns_width)
            parent_win->SetSize(new_size);
    }
}

TopBarItemsCtrl::TopBarItemsCtrl(wxWindow *parent, TopBarMenus* menus/* = nullptr*/, std::function<void()> cb_settings_btn/* = nullptr*/) :
    wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
    ,m_menus(menus)
    ,m_cb_settings_btn(cb_settings_btn)
{
    wxGetApp().UpdateDarkUI(this);

#ifdef _WIN32
    SetDoubleBuffered(true);
#endif //__WINDOWS__
    update_margins();

    m_sizer = new wxFlexGridSizer(2);
    m_sizer->AddGrowableCol(0);
    m_sizer->SetFlexibleDirection(wxHORIZONTAL);
    this->SetSizer(m_sizer);

    wxBoxSizer* left_sizer = new wxBoxSizer(wxHORIZONTAL);
/*
#ifdef __APPLE__
    auto logo = new wxStaticBitmap(this, wxID_ANY, *get_bmp_bundle(wxGetApp().logo_name(), 40));
    left_sizer->Add(logo, 0, wxALIGN_CENTER_VERTICAL | wxALL, m_btn_margin);
#else
    m_menu_btn = new ButtonWithPopup(this, _L("Menu"), wxGetApp().logo_name());
    left_sizer->Add(m_menu_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, m_btn_margin);
    
    m_menu_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        m_menu_btn->set_selected(true);
        m_menus->Popup(this, &m_menus->main, m_menu_btn->get_popup_pos());
    });
#endif
*/
    if (m_cb_settings_btn) {
        m_settings_btn = new Button(this, _L("Settings"/*, "settings"*/));
        m_settings_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) { m_cb_settings_btn(); });
        left_sizer->Add(m_settings_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, m_btn_margin);
    }

    m_buttons_sizer = new wxFlexGridSizer(1, m_btn_margin, m_btn_margin);
    left_sizer->Add(m_buttons_sizer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, m_btn_margin);

    CreateSearch();
    if (m_cb_settings_btn)
        wxGetApp().searcher().set_search_input(m_search);

    wxBoxSizer* search_sizer = new wxBoxSizer(wxVERTICAL);
    search_sizer->Add(m_search, 1, wxEXPAND | wxALIGN_RIGHT);
    left_sizer->Add(search_sizer, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, m_btn_margin);

    m_sizer->Add(left_sizer, 1, wxEXPAND);

    wxBoxSizer* right_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_workspace_btn = new ButtonWithPopup(this, "Workspace", "mode_simple");
    right_sizer->AddStretchSpacer(20);
    right_sizer->Add(m_workspace_btn, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, m_btn_margin);
    
    m_workspace_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        m_workspace_btn->set_selected(true);
        m_menus->Popup(this, &m_menus->workspaces, m_workspace_btn->get_popup_pos());
    });

    m_account_btn = new ButtonWithPopup(this, _L("Log in"), "user", login_icon_sz, wxSize(180, -1));
    right_sizer->Add(m_account_btn, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxRIGHT, m_btn_margin);
    
    m_account_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        m_account_btn->set_selected(true);
        m_menus->Popup(this, &m_menus->account, m_account_btn->get_popup_pos());
    });

    m_sizer->Add(right_sizer, 0, wxALIGN_CENTER_VERTICAL);

    m_sizer->SetItemMinSize(1, wxSize(42 * wxGetApp().em_unit(), -1));

    update_btns_width();
}

void TopBarItemsCtrl::UpdateMode()
{
    wxBitmapBundle bmp = *m_menus->get_workspace_bitmap();
    m_workspace_btn->SetBitmapBundle(bmp);
    m_workspace_btn->SetLabel(m_collapsed_btns ? "" : m_menus->get_workspace_name());

    this->Layout();
}

void TopBarItemsCtrl::ShowUserAccount(bool show)
{
    m_account_btn->Show(show);
    this->Layout();
}

void TopBarItemsCtrl::Rescale()
{
    update_margins();

    int em = em_unit(this);
    m_search->SetMinSize(wxSize(4 * em, -1));
    m_search->SetMaxSize(wxSize(42 * em, -1));
    m_search->Rescale();

    m_buttons_sizer->SetVGap(m_btn_margin);
    m_buttons_sizer->SetHGap(m_btn_margin);

    // call Layout before update buttons width to process recaling of the buttons
    m_sizer->Layout();

    update_btns_width();
    UpdateSearchSizeAndPosition();
    m_sizer->Layout();
}

void TopBarItemsCtrl::OnColorsChanged()
{
    wxGetApp().UpdateDarkUI(this);

    if (m_menus)
        m_menus->sys_color_changed();

    if (m_menu_btn)
        m_menu_btn->sys_color_changed();
    if (m_settings_btn)
        m_settings_btn->sys_color_changed();

    m_workspace_btn->sys_color_changed();
    m_account_btn->sys_color_changed();
    UpdateAccountButton(true);

    m_search->SysColorsChanged();

    UpdateSelection();
    UpdateMode();

    m_sizer->Layout();
}

void TopBarItemsCtrl::UpdateModeMarkers()
{
    UpdateMode();
    m_menus->ApplyWorkspacesMenu();
}

void TopBarItemsCtrl::UpdateSelection()
{
    for (Button* btn : m_pageButtons)
        btn->set_selected(false);

    if (m_selection >= 0)
        m_pageButtons[m_selection]->set_selected(true);

    Refresh();
}

void TopBarItemsCtrl::SetSelection(int sel, bool force /*= false*/)
{
    if (m_selection == sel && !force)
        return;
    m_selection = sel;
    UpdateSelection();
}

bool TopBarItemsCtrl::InsertPage(size_t n, const wxString& text, bool bSelect/* = false*/, const std::string& bmp_name/* = ""*/)
{
    Button* btn = new Button(this, text);
    btn->Bind(wxEVT_BUTTON, [this, btn](wxCommandEvent& event) {
        if (auto it = std::find(m_pageButtons.begin(), m_pageButtons.end(), btn); it != m_pageButtons.end()) {
            m_selection = it - m_pageButtons.begin();
            wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_TOPBAR_SEL_CHANGED);
            evt.SetId(m_selection);
            wxPostEvent(this->GetParent(), evt);
            UpdateSelection();
        }
    });

    m_pageButtons.insert(m_pageButtons.begin() + n, btn);
    m_buttons_sizer->Insert(n, new wxSizerItem(btn, 0, wxALIGN_CENTER_VERTICAL));
    m_buttons_sizer->SetCols(m_buttons_sizer->GetCols() + 1);

    update_btns_width();
    UpdateSearchSizeAndPosition();
    m_sizer->Layout();
    return true;
}

void TopBarItemsCtrl::RemovePage(size_t n)
{
    auto btn = m_pageButtons[n];
    m_pageButtons.erase(m_pageButtons.begin() + n);
    m_buttons_sizer->Remove(n);

    // Under OSX call of btn->Reparent(nullptr) causes a crash, so as a workaround use RemoveChild() instead
    this->RemoveChild(btn);
    btn->Destroy();

    update_btns_width();
    UpdateSearchSizeAndPosition();
    m_sizer->Layout();
}

void TopBarItemsCtrl::SetPageText(size_t n, const wxString& strText)
{
    auto btn = m_pageButtons[n];
    btn->SetLabel(strText);
    update_btns_width();
    UpdateSearchSizeAndPosition();
}

wxString TopBarItemsCtrl::GetPageText(size_t n) const
{
    auto btn = m_pageButtons[n];
    return btn->GetLabel();
}

void TopBarItemsCtrl::ShowFull()
{
    if (m_menu_btn)
        m_menu_btn->Show();
    if (m_settings_btn)
        m_settings_btn->Show();
    m_account_btn->Show();
    update_btns_width();
    UpdateSearchSizeAndPosition();
}

void TopBarItemsCtrl::ShowJustMode()
{
    if (m_menu_btn)
        m_menu_btn->Hide();
    if (m_settings_btn)
        m_settings_btn->Hide();
    m_account_btn->Hide();
    update_btns_width();
    UpdateSearchSizeAndPosition();
}

void TopBarItemsCtrl::SetSettingsButtonTooltip(const wxString& tooltip)
{
    if (m_settings_btn)
        m_settings_btn->SetToolTip(tooltip);
}
