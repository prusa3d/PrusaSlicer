#pragma once

#include "StateHandler.hpp"

#include <wx/window.h>

namespace Slic3r::App::WX::Widgets {

class StaticBox : public wxWindow
{
public:
    StaticBox();

    StaticBox(wxWindow* parent,
             wxWindowID      id        = wxID_ANY,
             const wxPoint & pos       = wxDefaultPosition,
             const wxSize &  size      = wxDefaultSize, 
             long style = 0);

    bool Create(wxWindow* parent,
        wxWindowID      id        = wxID_ANY,
        const wxPoint & pos       = wxDefaultPosition,
        const wxSize &  size      = wxDefaultSize, 
        long style = 0);

    void SetCornerRadius(double radius);

    void SetBorderWidth(int width);

    void SetBorderColor(StateColor const & color);

    void SetBorderColorNormal(wxColor const &color);

    virtual void SetBackgroundColor(StateColor const &color);

    void SetBackgroundColorNormal(wxColor const &color);

    void SetBackgroundColor2(StateColor const &color);

    static wxColor GetParentBackgroundColor(wxWindow * parent);

protected:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    virtual void doRender(wxDC& dc);

    double radius;
    int border_width = 1;
    StateHandler state_handler;
    StateColor   border_color;
    StateColor   background_color;
    StateColor   background_color2;

    DECLARE_EVENT_TABLE()
};

}
