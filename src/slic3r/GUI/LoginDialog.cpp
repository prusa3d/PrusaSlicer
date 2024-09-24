#include "LoginDialog.hpp"

#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "format.hpp"

namespace Slic3r {
namespace GUI {

LoginDialog::LoginDialog(wxWindow* parent, UserAccount* user_account)
    // TRN: This is the dialog title.
     : DPIDialog(parent, wxID_ANY, ("Prusa Account"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
     , p_user_account(user_account)
{
    const int em = wxGetApp().em_unit();
    bool logged = p_user_account->is_logged();
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    // sizer with black border
    wxStaticBoxSizer* static_box_sizer = new wxStaticBoxSizer(wxVERTICAL, this, ("Log into your Prusa Account"));
    static_box_sizer->SetMinSize(wxSize(em * 30, em * 15));
    // avatar
    boost::filesystem::path path = p_user_account->get_avatar_path(logged);
    ScalableBitmap logo(this, path, wxSize(em * 10, em * 10));
    m_avatar_bitmap = new wxStaticBitmap(this, wxID_ANY, logo.bmp(), wxDefaultPosition, wxDefaultSize);
    static_box_sizer->Add(m_avatar_bitmap, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);
    // username
    const wxString username = GUI::format_wxstr("%1%", logged ? from_u8(p_user_account->get_username()) : ("Anonymous"));
    m_username_label = new wxStaticText(this, wxID_ANY, username, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_username_label->SetFont(m_username_label->GetFont().Bold());
    static_box_sizer->Add(m_username_label, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 5);
    // login button
    m_login_button_id = NewControlId();
    m_login_button = new wxButton(this, m_login_button_id, logged ? ("Log out") : ("Log in"));
    static_box_sizer->Add(m_login_button, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);
    // TODO: why is m_login_button always hovered?
    main_sizer->Add(static_box_sizer, 1, wxEXPAND | wxALL, 10);
    // continue button
    m_continue_button = new wxButton(this, wxID_OK, logged ? ("Continue") : ("Continue without Prusa Account"));
    main_sizer->Add(m_continue_button, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);

    SetSizerAndFit(main_sizer);

    m_login_button->Bind(wxEVT_BUTTON, [user_account = p_user_account](wxCommandEvent& event) {
        if (!user_account->is_logged())
            user_account->do_login();
        else
            user_account->do_logout();
    });

    wxGetApp().UpdateDlgDarkUI(this);
    SetFocus();
}

LoginDialog::~LoginDialog()
{
}

void LoginDialog::update_account()
{
    bool logged = p_user_account->is_logged();

    const wxString username = GUI::format_wxstr("%1%", logged ? from_u8(p_user_account->get_username()) : ("Anonymous"));
    m_username_label->SetLabel(username);

    boost::filesystem::path path = p_user_account->get_avatar_path(logged);
    if (boost::filesystem::exists(path)) {
        const int em = wxGetApp().em_unit();
        ScalableBitmap logo(this, path, wxSize(em * 10, em * 10));
        m_avatar_bitmap->SetBitmap(logo.bmp());
    }

    m_login_button->SetLabel(logged ? ("Log out") : ("Log in"));
    m_continue_button->SetLabel(logged ? ("Continue") : ("Continue without Prusa Account"));
    // TODO: resize correctly m_continue_button 
    //m_continue_button->Fit();

    Fit();
    Refresh();
}

void LoginDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    
    SetFont(wxGetApp().normal_font());

    const int em = em_unit();
    msw_buttons_rescale(this, em, { wxID_OK, m_login_button_id});

    Fit();
    Refresh();
    
}
}}// Slicer::GUI