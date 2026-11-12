#include "Slic3r/App/WX/Highlighter.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"

#include <wx/panel.h>

namespace Slic3r::App::WX {

// ----------------------------------------------------------------------------
// BlinkingBitmap
// ----------------------------------------------------------------------------

BlinkingBitmap::BlinkingBitmap(wxWindow* parent, const std::string& icon_name) :
    wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(int(1.6 * w_config()->em_unit()), -1))
{
    bmp = ScalableBitmap(parent, icon_name);
}

void BlinkingBitmap::invalidate()
{
    this->SetBitmap(wxNullBitmap);
}

void BlinkingBitmap::activate()
{
    this->SetBitmap(bmp.bmp());
    show = true;
}

void BlinkingBitmap::blink()
{
    show = !show;
    this->SetBitmap(show ? bmp.bmp() : wxNullBitmap);
}

void Highlighter::set_timer_owner(wxWindow* owner, int timerid/* = wxID_ANY*/)
{
    m_timer.SetOwner(owner, timerid);
    bind_timer(owner);
}

bool Highlighter::init(bool input_failed)
{
    if (input_failed)
        return false;

    m_timer.Start(300, false);
    return true;
}
void Highlighter::invalidate()
{
    if (m_timer.IsRunning())
        m_timer.Stop();
    m_blink_counter = 0;
}

void Highlighter::blink()
{
    if ((++m_blink_counter) == 11)
        invalidate();
}

// HighlighterForWx

void HighlighterForWx::bind_timer(wxWindow* owner)
{
    owner->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        blink();
    });
}

// using OG_CustomCtrl where arrow will be rendered and flag indicated "show/hide" state of this arrow
void HighlighterForWx::init(std::pair<wxPanel*, bool*> params)
{
    invalidate();
    if (!Highlighter::init(!params.first && !params.second))
        return;

    m_custom_ctrl = params.first;
    m_show_blink_ptr = params.second;

    *m_show_blink_ptr = true;
    m_custom_ctrl->Refresh();
}

// - using a BlinkingBitmap. Change state of this bitmap
void HighlighterForWx::init(BlinkingBitmap* blinking_bmp)
{
    invalidate();
    if (!Highlighter::init(!blinking_bmp))
        return;

    m_blinking_bitmap = blinking_bmp;
    m_blinking_bitmap->activate();
}

void HighlighterForWx::invalidate()
{
    Highlighter::invalidate();

    if (m_custom_ctrl && m_show_blink_ptr) {
        *m_show_blink_ptr = false;
//        m_custom_ctrl->Refresh();
        m_show_blink_ptr = nullptr;
        m_custom_ctrl = nullptr;
    }
    else if (m_blinking_bitmap) {
        m_blinking_bitmap->invalidate();
        m_blinking_bitmap = nullptr;
    }
}

void HighlighterForWx::blink()
{
    if (m_custom_ctrl && m_show_blink_ptr) {
        *m_show_blink_ptr = !*m_show_blink_ptr;
//        m_custom_ctrl->Refresh();
    }
    else if (m_blinking_bitmap)
        m_blinking_bitmap->blink();
    else
        return;

    Highlighter::blink();
}


}


