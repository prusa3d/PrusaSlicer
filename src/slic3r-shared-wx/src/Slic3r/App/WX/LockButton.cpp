#include "Slic3r/App/WX/LockButton.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"

namespace Slic3r::App::WX {

LockButton::LockButton( wxWindow *parent, 
                        wxWindowID id, 
                        const wxPoint& pos /*= wxDefaultPosition*/, 
                        const wxSize& size /*= wxDefaultSize*/):
                        wxButton(parent, id, wxEmptyString, pos, size, wxBU_EXACTFIT | wxNO_BORDER)
{
    m_bmp_lock_closed   = ScalableBitmap(this, "lock_closed");
    m_bmp_lock_closed_f = ScalableBitmap(this, "lock_closed_f");
    m_bmp_lock_open     = ScalableBitmap(this, "lock_open");
    m_bmp_lock_open_f   = ScalableBitmap(this, "lock_open_f");

    SetBitmap(m_bmp_lock_open.bmp());
    SetBitmapDisabled(m_bmp_lock_open.bmp());
    SetBitmapCurrent(m_bmp_lock_closed_f.bmp());

    //button events
    Bind(wxEVT_BUTTON, &LockButton::OnButton, this);
}

void LockButton::OnButton(wxCommandEvent& event)
{
    if (m_disabled)
        return;

    SetLock(!m_is_pushed);
    event.Skip();
}

void LockButton::SetLock(bool lock)
{
    if (m_is_pushed != lock) {
        m_is_pushed = lock;
        update_button_bitmaps();
    }
}

void LockButton::sys_color_changed()
{
    m_bmp_lock_closed.sys_color_changed();
    m_bmp_lock_closed_f.sys_color_changed();
    m_bmp_lock_open.sys_color_changed();
    m_bmp_lock_open_f.sys_color_changed();

    update_button_bitmaps();
}

void LockButton::update_button_bitmaps()
{
    SetBitmap(m_is_pushed ? m_bmp_lock_closed.bmp() : m_bmp_lock_open.bmp());
    SetBitmapCurrent(m_is_pushed ? m_bmp_lock_closed_f.bmp() : m_bmp_lock_open_f.bmp());

    Refresh();
    Update();
}

}




