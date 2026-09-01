#include "GradientRatioBar.hpp"

#include <algorithm>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>

#include "../GUI_App.hpp"

namespace Slic3r::GUI {

GradientRatioBar::GradientRatioBar(wxWindow* parent) :
    wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, BAR_HEIGHT + 2 * HANDLE_OVERHANG))
{
    this->SetBackgroundStyle(wxBG_STYLE_PAINT);
    this->SetMinSize(wxSize(160, BAR_HEIGHT + 2 * HANDLE_OVERHANG));
    this->Bind(wxEVT_PAINT, &GradientRatioBar::on_paint, this);
    this->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent& e)
        {
            this->Refresh();
            e.Skip();
        }
    );
    this->Bind(wxEVT_LEFT_DOWN, &GradientRatioBar::on_left_down, this);
    this->Bind(wxEVT_LEFT_UP, &GradientRatioBar::on_left_up, this);
    this->Bind(wxEVT_MOTION, &GradientRatioBar::on_motion, this);
    this->Bind(wxEVT_MOUSE_CAPTURE_LOST, &GradientRatioBar::on_capture_lost, this);
}

void GradientRatioBar::SetColors(const wxColour& color_a, const wxColour& color_b)
{
    m_color_a = color_a;
    m_color_b = color_b;
    this->Refresh();
}

void GradientRatioBar::SetRatioAPercent(int ratio_a_percent)
{
    m_ratio_a_percent = snap_to_step(std::clamp(ratio_a_percent, MIN_RATIO, MAX_RATIO));
    this->Refresh();
}

int GradientRatioBar::snap_to_step(int value)
{
    const int s = ((value + STEP / 2) / STEP) * STEP;
    return std::clamp(s, MIN_RATIO, MAX_RATIO);
}

int GradientRatioBar::marker_x_for_ratio() const
{
    const int width  = this->GetClientSize().GetWidth();
    const int usable = std::max(1, width - HANDLE_W);
    return HANDLE_HALF_W + usable * (100 - m_ratio_a_percent) / 100;
}

void GradientRatioBar::update_from_x(int mouse_x, bool notify)
{
    const int width       = this->GetClientSize().GetWidth();
    const int usable      = std::max(1, width - HANDLE_W);
    const int clamped     = std::clamp(mouse_x - HANDLE_HALF_W, 0, usable);
    const int raw_ratio_b = (clamped * 100 + usable / 2) / usable;
    const int raw_ratio_a = 100 - raw_ratio_b;
    const int new_ratio_a = snap_to_step(raw_ratio_a);
    if (new_ratio_a == m_ratio_a_percent) {
        return;
    }

    m_ratio_a_percent = new_ratio_a;
    this->Refresh();
    if (notify) {
        wxCommandEvent event(wxEVT_SLIDER, this->GetId());
        event.SetInt(m_ratio_a_percent);
        event.SetEventObject(this);
        this->ProcessWindowEvent(event);
    }
}

void GradientRatioBar::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize sz = this->GetClientSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) {
        return;
    }

    const wxRect bar_rect(
        HANDLE_HALF_W,
        HANDLE_OVERHANG,
        sz.GetWidth() - HANDLE_W,
        sz.GetHeight() - 2 * HANDLE_OVERHANG
    );

    const bool dark           = GUI_App::dark_mode();
    const wxColour border_clr = dark ? wxColour(0x70, 0x70, 0x70) : wxColour(0xAA, 0xAA, 0xAA);
    const wxColour handle_clr = dark ? wxColour(0x50, 0x50, 0x50) : wxColour(0xE0, 0xE0, 0xE0);

    dc.SetBackground(wxBrush(this->GetParent()->GetBackgroundColour()));
    dc.Clear();
    dc.GradientFillLinear(bar_rect, m_color_a, m_color_b, wxEAST);

    dc.SetPen(wxPen(border_clr, 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(bar_rect);

    const int marker_x = marker_x_for_ratio();
    const int hx       = marker_x - HANDLE_HALF_W;
    dc.SetBrush(wxBrush(handle_clr));
    dc.SetPen(wxPen(border_clr, 1));
    dc.DrawRoundedRectangle(hx, 0, HANDLE_W, sz.GetHeight(), 2);
}

void GradientRatioBar::on_left_down(wxMouseEvent& event)
{
    if (!this->HasCapture()) {
        this->CaptureMouse();
    }

    m_dragging = true;
    update_from_x(event.GetX(), true);
}

void GradientRatioBar::on_motion(wxMouseEvent& event)
{
    if (!m_dragging || !event.LeftIsDown()) {
        return;
    }

    update_from_x(event.GetX(), true);
}

void GradientRatioBar::on_left_up(wxMouseEvent& event)
{
    if (m_dragging) {
        update_from_x(event.GetX(), true);
    }

    m_dragging = false;
    if (this->HasCapture()) {
        this->ReleaseMouse();
    }
}

void GradientRatioBar::on_capture_lost(wxMouseCaptureLostEvent&)
{
    m_dragging = false;
}

} // namespace Slic3r::GUI
