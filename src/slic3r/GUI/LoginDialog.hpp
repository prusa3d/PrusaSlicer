#ifndef slic3r_LoginDialog_hpp_
#define slic3r_LoginDialog_hpp_

#include "UserAccount.hpp"

#include "GUI_Utils.hpp"

#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>

namespace Slic3r {
namespace GUI {

class RemovableDriveManager;
class LoginDialog : public DPIDialog
{
public:
    LoginDialog(wxWindow* parent, UserAccount* user_account);
    ~LoginDialog();

    void update_account();
private:
    UserAccount*    p_user_account;

    wxStaticText*   m_username_label;
    wxStaticBitmap* m_avatar_bitmap;
    wxButton*       m_login_button;
    int             m_login_button_id{ wxID_ANY };
    wxButton*       m_continue_button;
protected:
    void        on_dpi_changed(const wxRect& suggested_rect) override;
    void        on_sys_color_changed() override {}
};

}} // Slicer::GUI
#endif