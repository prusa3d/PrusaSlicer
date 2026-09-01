#include "BarycentricRatioPicker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>

#include "../ColorUtils.hpp"
#include "../GUI_App.hpp"

namespace Slic3r::GUI {

BarycentricRatioPicker::BarycentricRatioPicker(wxWindow* parent) :
    wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 180))
{
    this->SetBackgroundStyle(wxBG_STYLE_PAINT);
    this->SetMinSize(wxSize(180, 180));
    this->Bind(wxEVT_PAINT, &BarycentricRatioPicker::on_paint, this);
    this->Bind(
        wxEVT_SIZE,
        [this](wxSizeEvent& e)
        {
            this->Refresh();
            e.Skip();
        }
    );
    this->Bind(wxEVT_LEFT_DOWN, &BarycentricRatioPicker::on_left_down, this);
    this->Bind(wxEVT_LEFT_UP, &BarycentricRatioPicker::on_left_up, this);
    this->Bind(wxEVT_MOTION, &BarycentricRatioPicker::on_motion, this);
    this->Bind(wxEVT_MOUSE_CAPTURE_LOST, &BarycentricRatioPicker::on_capture_lost, this);
}

void BarycentricRatioPicker::SetColors(const wxColour& c0, const wxColour& c1, const wxColour& c2)
{
    if (c0 == m_c0 && c1 == m_c1 && c2 == m_c2) {
        return;
    }

    m_c0 = c0;
    m_c1 = c1;
    m_c2 = c2;
    this->Refresh();
}

void BarycentricRatioPicker::SetWeights(double w0, double w1, double w2)
{
    normalise_and_assign(w0, w1, w2);
    this->Refresh();
}

void
BarycentricRatioPicker::SetVertexLabels(const wxString& l0, const wxString& l1, const wxString& l2)
{
    m_label0 = l0;
    m_label1 = l1;
    m_label2 = l2;
    this->Refresh();
}

double BarycentricRatioPicker::signed_area2(TriPoint v0, TriPoint v1, TriPoint v2)
{
    return (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
}

bool BarycentricRatioPicker::contains(TriPoint p, TriPoint v0, TriPoint v1, TriPoint v2)
{
    const double a     = signed_area2(p, v0, v1);
    const double b     = signed_area2(p, v1, v2);
    const double c     = signed_area2(p, v2, v0);
    const bool has_neg = (a < 0) || (b < 0) || (c < 0);
    const bool has_pos = (a > 0) || (b > 0) || (c > 0);
    return !(has_neg && has_pos);
}

void BarycentricRatioPicker::barycentric(
    TriPoint p,
    TriPoint v0,
    TriPoint v1,
    TriPoint v2,
    double& w0,
    double& w1,
    double& w2
)
{
    const double total = signed_area2(v0, v1, v2);
    if (std::abs(total) < 1e-9) {
        w0 = w1 = w2 = 1.0 / 3.0;
        return;
    }

    w0 = signed_area2(p, v1, v2) / total;
    w1 = signed_area2(v0, p, v2) / total;
    w2 = 1.0 - w0 - w1;
}

void BarycentricRatioPicker::vertices(TriPoint& v0, TriPoint& v1, TriPoint& v2) const
{
    const wxSize sz     = this->GetClientSize();
    const double pw     = std::max(1, sz.GetWidth());
    const double ph     = std::max(1, sz.GetHeight());
    const double margin = 12.0;
    const double avail  = std::min(pw, ph) - 2.0 * margin;
    const double side   = std::max(1.0, avail);
    const double tri_h  = side * std::sqrt(3.0) / 2.0;
    const double cx     = pw / 2.0;
    const double top_y  = (ph - tri_h) / 2.0;
    const double bot_y  = top_y + tri_h;
    v0                  = {cx, top_y};
    v1                  = {cx - side / 2.0, bot_y};
    v2                  = {cx + side / 2.0, bot_y};
}

void BarycentricRatioPicker::normalise_and_assign(double w0, double w1, double w2)
{
    w0             = std::clamp(w0, 0.0, 1.0);
    w1             = std::clamp(w1, 0.0, 1.0);
    w2             = std::clamp(w2, 0.0, 1.0);
    const double s = w0 + w1 + w2;
    if (s > 0) {
        m_w0 = w0 / s;
        m_w1 = w1 / s;
        m_w2 = w2 / s;
    } else {
        m_w0 = m_w1 = m_w2 = 1.0 / 3.0;
    }
}

void BarycentricRatioPicker::snap_weights_5pct(double& w0, double& w1, double& w2)
{
    constexpr double third       = 1.0 / 3.0;
    constexpr double center_band = 0.05;
    if (std::abs(w0 - third) < center_band
        && std::abs(w1 - third) < center_band
        && std::abs(w2 - third) < center_band)
    {
        w0 = w1 = w2 = third;
        return;
    }

    constexpr int total_tokens = 20;
    const double t0            = w0 * total_tokens;
    const double t1            = w1 * total_tokens;
    const double t2            = w2 * total_tokens;
    int p0                     = int(std::floor(t0));
    int p1                     = int(std::floor(t1));
    int p2                     = int(std::floor(t2));
    p0                         = std::clamp(p0, 0, total_tokens);
    p1                         = std::clamp(p1, 0, total_tokens);
    p2                         = std::clamp(p2, 0, total_tokens);

    int leftover = total_tokens - (p0 + p1 + p2);
    std::array<std::pair<double, int>, 3> rem{{
        {t0 - std::floor(t0), 0},
        {t1 - std::floor(t1), 1},
        {t2 - std::floor(t2), 2},
    }};
    std::sort(
        rem.begin(),
        rem.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; }
    );
    for (int i = 0; i < 3 && leftover > 0; ++i, --leftover) {
        if (rem[i].second == 0) {
            ++p0;
        } else if (rem[i].second == 1) {
            ++p1;
        } else {
            ++p2;
        }
    }
    std::sort(
        rem.begin(),
        rem.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; }
    );
    for (int i = 0; i < 3 && leftover < 0; ++i, ++leftover) {
        if (rem[i].second == 0 && p0 > 0) {
            --p0;
        } else if (rem[i].second == 1 && p1 > 0) {
            --p1;
        } else if (rem[i].second == 2 && p2 > 0) {
            --p2;
        } else
            ++leftover;
    }

    w0 = double(p0) / double(total_tokens);
    w1 = double(p1) / double(total_tokens);
    w2 = double(p2) / double(total_tokens);
}

void BarycentricRatioPicker::update_from_point(TriPoint p, bool notify)
{
    TriPoint v0, v1, v2;
    vertices(v0, v1, v2);
    double w0, w1, w2;
    barycentric(p, v0, v1, v2, w0, w1, w2);
    w0             = std::clamp(w0, 0.0, 1.0);
    w1             = std::clamp(w1, 0.0, 1.0);
    w2             = std::clamp(w2, 0.0, 1.0);
    const double s = w0 + w1 + w2;
    if (s > 0) {
        w0 /= s;
        w1 /= s;
        w2 /= s;
    } else {
        w0 = w1 = w2 = 1.0 / 3.0;
    }
    snap_weights_5pct(w0, w1, w2);
    if (std::abs(w0 - m_w0) < 1e-6 && std::abs(w1 - m_w1) < 1e-6 && std::abs(w2 - m_w2) < 1e-6) {
        return;
    }

    m_w0 = w0;
    m_w1 = w1;
    m_w2 = w2;
    this->Refresh();
    if (notify) {
        wxCommandEvent event(wxEVT_SLIDER, this->GetId());
        event.SetEventObject(this);
        this->ProcessWindowEvent(event);
    }
}

void BarycentricRatioPicker::draw_vertex_badge(
    wxDC& dc,
    double vx,
    double vy,
    const wxColour& fill,
    const wxString& label
) const
{
    const int radius = 11;
    const int cx     = int(std::round(vx));
    const int cy     = int(std::round(vy));
    const wxColour badge_border =
        GUI_App::dark_mode() ? wxColour(0xC0, 0xC0, 0xC0) : wxColour(0x20, 0x20, 0x20);
    dc.SetBrush(wxBrush(fill));
    dc.SetPen(wxPen(badge_border, 2));
    dc.DrawCircle(cx, cy, radius);

    dc.SetFont(wxGetApp().small_font().Bold());
    dc.SetTextForeground(wcag_contrast_text(fill));
    wxCoord text_w = 0, text_h = 0;
    dc.GetTextExtent(label, &text_w, &text_h);
    dc.DrawText(label, cx - text_w / 2, cy - text_h / 2);
}

void BarycentricRatioPicker::on_paint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize sz = this->GetClientSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) {
        return;
    }

    dc.SetBackground(wxBrush(this->GetParent()->GetBackgroundColour()));
    dc.Clear();

    TriPoint v0, v1, v2;
    vertices(v0, v1, v2);

    const int min_x = int(std::floor(std::min({v0.x, v1.x, v2.x}))) - 1;
    const int max_x = int(std::ceil(std::max({v0.x, v1.x, v2.x}))) + 1;
    const int min_y = int(std::floor(std::min({v0.y, v1.y, v2.y}))) - 1;
    const int max_y = int(std::ceil(std::max({v0.y, v1.y, v2.y}))) + 1;
    const int bmp_w = std::max(1, max_x - min_x + 1);
    const int bmp_h = std::max(1, max_y - min_y + 1);

    wxImage img(bmp_w, bmp_h);
    img.InitAlpha();
    unsigned char* rgb   = img.GetData();
    unsigned char* alpha = img.GetAlpha();
    std::memset(alpha, 0, size_t(bmp_w) * size_t(bmp_h));

    for (int py = min_y; py <= max_y; ++py) {
        for (int px = min_x; px <= max_x; ++px) {
            TriPoint p{double(px), double(py)};
            if (!contains(p, v0, v1, v2)) {
                continue;
            }

            double w0, w1, w2;
            barycentric(p, v0, v1, v2, w0, w1, w2);
            w0 = std::clamp(w0, 0.0, 1.0);
            w1 = std::clamp(w1, 0.0, 1.0);
            w2 = std::clamp(w2, 0.0, 1.0);
            const int r =
                std::clamp(int(m_c0.Red() * w0 + m_c1.Red() * w1 + m_c2.Red() * w2 + 0.5), 0, 255);
            const int g = std::clamp(
                int(m_c0.Green() * w0 + m_c1.Green() * w1 + m_c2.Green() * w2 + 0.5),
                0,
                255
            );
            const int b = std::clamp(
                int(m_c0.Blue() * w0 + m_c1.Blue() * w1 + m_c2.Blue() * w2 + 0.5),
                0,
                255
            );
            const size_t idx = (size_t(py - min_y) * size_t(bmp_w) + size_t(px - min_x));
            rgb[idx * 3 + 0] = (unsigned char) r;
            rgb[idx * 3 + 1] = (unsigned char) g;
            rgb[idx * 3 + 2] = (unsigned char) b;
            alpha[idx]       = 255;
        }
    }
    dc.DrawBitmap(wxBitmap(img), min_x, min_y, /*useMask=*/true);

    wxPoint outline[3] = {
        wxPoint(int(std::round(v0.x)), int(std::round(v0.y))),
        wxPoint(int(std::round(v1.x)), int(std::round(v1.y))),
        wxPoint(int(std::round(v2.x)), int(std::round(v2.y))),
    };
    const bool dark = GUI_App::dark_mode();
    dc.SetPen(wxPen(dark ? wxColour(0x90, 0x90, 0x90) : wxColour(0x60, 0x60, 0x60), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawPolygon(3, outline);

    draw_vertex_badge(dc, v0.x, v0.y, m_c0, m_label0);
    draw_vertex_badge(dc, v1.x, v1.y, m_c1, m_label1);
    draw_vertex_badge(dc, v2.x, v2.y, m_c2, m_label2);

    const double hx    = m_w0 * v0.x + m_w1 * v1.x + m_w2 * v2.x;
    const double hy    = m_w0 * v0.y + m_w1 * v1.y + m_w2 * v2.y;
    const int handle_r = 6;
    dc.SetBrush(dark ? wxBrush(wxColour(0xD0, 0xD0, 0xD0)) : *wxWHITE_BRUSH);
    dc.SetPen(wxPen(dark ? wxColour(0xC0, 0xC0, 0xC0) : wxColour(0x20, 0x20, 0x20), 2));
    dc.DrawCircle(int(std::round(hx)), int(std::round(hy)), handle_r);
}

void BarycentricRatioPicker::on_left_down(wxMouseEvent& event)
{
    TriPoint v0, v1, v2;
    vertices(v0, v1, v2);
    TriPoint p{double(event.GetX()), double(event.GetY())};
    if (!contains(p, v0, v1, v2)) {
        return;
    }

    if (!this->HasCapture()) {
        this->CaptureMouse();
    }

    m_dragging = true;
    update_from_point(p, true);
}

void BarycentricRatioPicker::on_motion(wxMouseEvent& event)
{
    if (!m_dragging || !event.LeftIsDown()) {
        return;
    }

    update_from_point(TriPoint{double(event.GetX()), double(event.GetY())}, true);
}

void BarycentricRatioPicker::on_left_up(wxMouseEvent& event)
{
    if (m_dragging) {
        update_from_point(TriPoint{double(event.GetX()), double(event.GetY())}, true);
    }

    m_dragging = false;

    if (this->HasCapture()) {
        this->ReleaseMouse();
    }
}

void BarycentricRatioPicker::on_capture_lost(wxMouseCaptureLostEvent&)
{
    m_dragging = false;
}

} // namespace Slic3r::GUI
