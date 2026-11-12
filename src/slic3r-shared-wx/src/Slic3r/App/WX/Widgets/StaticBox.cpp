#include "Slic3r/App/WX/Widgets/StaticBox.hpp"
#include <wx/dcgraph.h>
#include <wx/dcbuffer.h>

#include "Slic3r/App/WX/Widgets/UIColors.hpp"

namespace Slic3r::App::WX::Widgets {

BEGIN_EVENT_TABLE(StaticBox, wxWindow)

EVT_PAINT(StaticBox::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

StaticBox::StaticBox()
    : radius(8)
    , state_handler(this)
{
    border_color = StateColor(std::make_pair(clr_border_disabled,   (int) StateColor::Disabled),
#ifndef __WXMSW__
                              std::make_pair(clr_border_normal,     (int) StateColor::Focused),
#endif
                              std::make_pair(clr_border_hovered,    (int) StateColor::Hovered),
                              std::make_pair(clr_border_normal,     (int) StateColor::Normal));
#ifndef __WXMSW__
    border_color.setTakeFocusedAsHovered(false);
#endif
}

StaticBox::StaticBox(wxWindow* parent,
                   wxWindowID      id,
                   const wxPoint & pos,
                   const wxSize &  size, long style)
    : StaticBox()
{
    Create(parent, id, pos, size, style);
}

bool StaticBox::Create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
{
    if (style & wxBORDER_NONE)
        border_width = 0;
    wxWindow::Create(parent, id, pos, size, style);
    state_handler.attach({&border_color, &background_color, &background_color2});
    state_handler.update_binds();
//    SetBackgroundColour(GetParentBackgroundColor(parent));
//    SetForegroundColour(parent->GetParent()->GetForegroundColour());
    return true;
}

void StaticBox::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void StaticBox::SetBorderWidth(int width)
{
    border_width = width;
    Refresh();
}

void StaticBox::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void StaticBox::SetBorderColorNormal(wxColor const &color)
{
    border_color.setColorForStates(color, 0);
    Refresh();
}

void StaticBox::SetBackgroundColor(StateColor const &color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

void StaticBox::SetBackgroundColorNormal(wxColor const &color)
{
    background_color.setColorForStates(color, 0);
    Refresh();
}

void StaticBox::SetBackgroundColor2(StateColor const &color)
{
    background_color2 = color;
    state_handler.update_binds();
    Refresh();
}

wxColor StaticBox::GetParentBackgroundColor(wxWindow* parent)
{
    if (auto box = dynamic_cast<StaticBox*>(parent)) {
        if (box->background_color.count() > 0) {
            if (box->background_color2.count() == 0)
                return box->background_color.defaultColor();
            auto s = box->background_color.defaultColor();
            auto e = box->background_color2.defaultColor();
            int r = (s.Red() + e.Red()) / 2;
            int g = (s.Green() + e.Green()) / 2;
            int b = (s.Blue() + e.Blue()) / 2;
            return wxColor(r, g, b);
        }
    }
    if (parent)
        return parent->GetBackgroundColour();
    return *wxWHITE;
}

void StaticBox::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxBufferedPaintDC dc(this);//wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void StaticBox::render(wxDC& dc)
{
    doRender(dc);
}

void StaticBox::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    int states = state_handler.states();
    if (background_color2.count() == 0) {
        if ((border_width && border_color.count() > 0) || background_color.count() > 0) {
            wxRect rc(0, 0, size.x, size.y);
#ifdef __WXOSX__
            // On Retina displays all controls are cut on 1px
            if (dc.GetContentScaleFactor() > 1.)
                rc.Deflate(1, 1);
#endif //__WXOSX__

            if (radius > 0.) {
#ifdef __WXMSW__
                wxColour bg_clr = GetParentBackgroundColor(m_parent);
                dc.SetBrush(wxBrush(bg_clr));
                dc.SetPen(wxPen(bg_clr));
                dc.DrawRectangle(rc);
#endif
            }

            if (background_color.count() > 0)
                dc.SetBrush(wxBrush(background_color.colorForStates(states)));
            else
                dc.SetBrush(wxBrush(GetBackgroundColour()));

            if (border_width && border_color.count() > 0) {
#ifdef __WXOSX__
                const double bw = (double)border_width;
#else
                const double bw = dc.GetContentScaleFactor() * (double)border_width;
#endif //__WXOSX__
                {
                    int d  = floor(bw / 2.0);
                    int d2 = floor(bw - 1);
                    rc.x += d;
                    rc.width -= d2;
                    rc.y += d;
                    rc.height -= d2;
                }
                dc.SetPen(wxPen(border_color.colorForStates(states), bw));
            } else {
                dc.SetPen(wxPen(background_color.colorForStates(states)));
            }

            if (radius == 0.)
                dc.DrawRectangle(rc);
            else
                dc.DrawRoundedRectangle(rc, radius - border_width);
        }
    }
    else {
        wxColor start = background_color.colorForStates(states);
        wxColor stop = background_color2.colorForStates(states);
        int r = start.Red(), g = start.Green(), b = start.Blue();
        int dr = (int) stop.Red() - r, dg = (int) stop.Green() - g, db = (int) stop.Blue() - b;
        int lr = 0, lg = 0, lb = 0;
        for (int y = 0; y < size.y; ++y) {
            dc.SetPen(wxPen(wxColor(r, g, b)));
            dc.DrawLine(0, y, size.x, y);
            lr += dr; while (lr >= size.y) { ++r, lr -= size.y; } while (lr <= -size.y) { --r, lr += size.y; }
            lg += dg; while (lg >= size.y) { ++g, lg -= size.y; } while (lg <= -size.y) { --g, lg += size.y; }
            lb += db; while (lb >= size.y) { ++b, lb -= size.y; } while (lb <= -size.y) { --b, lb += size.y; }
        }
    }
}

}
