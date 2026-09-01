#pragma once

#include <wx/panel.h>
#include <wx/colour.h>
#include <wx/string.h>

namespace Slic3r::GUI {

class BarycentricRatioPicker : public wxPanel
{
public:
    BarycentricRatioPicker(wxWindow* parent);

    void SetColors(const wxColour& c0, const wxColour& c1, const wxColour& c2);
    void SetWeights(double w0, double w1, double w2);
    void SetVertexLabels(const wxString& l0, const wxString& l1, const wxString& l2);

    double weight_0() const
    {
        return m_w0;
    }

    double weight_1() const
    {
        return m_w1;
    }

    double weight_2() const
    {
        return m_w2;
    }

private:
    struct TriPoint
    {
        double x;
        double y;
    };

    wxColour m_c0{0xC0, 0xC0, 0xC0};
    wxColour m_c1{0xC0, 0xC0, 0xC0};
    wxColour m_c2{0xC0, 0xC0, 0xC0};
    double m_w0{1.0 / 3.0};
    double m_w1{1.0 / 3.0};
    double m_w2{1.0 / 3.0};
    bool m_dragging{false};
    wxString m_label0{"1"};
    wxString m_label1{"2"};
    wxString m_label2{"3"};

    static double signed_area2(TriPoint v0, TriPoint v1, TriPoint v2);
    static bool contains(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2);
    static void barycentric(
        TriPoint p,
        TriPoint v0,
        TriPoint v1,
        TriPoint v2,
        double& w0,
        double& w1,
        double& w2
    );
    static void snap_weights_5pct(double& w0, double& w1, double& w2);

    void vertices(TriPoint& v0, TriPoint& v1, TriPoint& v2) const;
    void normalise_and_assign(double w0, double w1, double w2);
    void update_from_point(TriPoint p, bool notify);
    void draw_vertex_badge(
        wxDC& dc,
        double vx,
        double vy,
        const wxColour& fill,
        const wxString& label
    ) const;

    void on_paint(wxPaintEvent&);
    void on_left_down(wxMouseEvent& event);
    void on_motion(wxMouseEvent& event);
    void on_left_up(wxMouseEvent& event);
    void on_capture_lost(wxMouseCaptureLostEvent&);
};

} // namespace Slic3r::GUI
