#pragma once

#include <wx/panel.h>
#include <wx/colour.h>

namespace Slic3r::GUI {

class GradientRatioBar : public wxPanel
{
public:
    static constexpr int HANDLE_OVERHANG = 2;
    static constexpr int HANDLE_W        = 10;
    static constexpr int HANDLE_HALF_W   = HANDLE_W / 2;

    GradientRatioBar(wxWindow* parent);

    void SetColors(const wxColour& color_a, const wxColour& color_b);
    void SetRatioAPercent(int ratio_a_percent);

    int GetRatioAPercent() const
    {
        return m_ratio_a_percent;
    }

private:
    static constexpr int BAR_HEIGHT = 28;
    static constexpr int STEP       = 5;
    static constexpr int MIN_RATIO  = 0;
    static constexpr int MAX_RATIO  = 100;

    wxColour m_color_a{0xC0, 0xC0, 0xC0};
    wxColour m_color_b{0x80, 0x80, 0x80};
    int m_ratio_a_percent{50};
    bool m_dragging{false};

    static int snap_to_step(int value);
    int marker_x_for_ratio() const;
    void update_from_x(int mouse_x, bool notify);

    void on_paint(wxPaintEvent&);
    void on_left_down(wxMouseEvent& event);
    void on_motion(wxMouseEvent& event);
    void on_left_up(wxMouseEvent& event);
    void on_capture_lost(wxMouseCaptureLostEvent&);
};

} // namespace Slic3r::GUI
