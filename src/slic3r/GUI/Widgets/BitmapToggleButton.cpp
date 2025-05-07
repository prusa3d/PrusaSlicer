#include "BitmapToggleButton.hpp"

#include <wx/settings.h>
#include <wx/button.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/setup.h>

#include "wx/window.h"

BitmapToggleButton::BitmapToggleButton(wxWindow* parent, const wxString& label, wxWindowID id)
{
    const long style = wxBORDER_NONE | wxBU_EXACTFIT | wxBU_LEFT;
    if (label.IsEmpty())
        wxBitmapToggleButton::Create(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, style);
    else {
#ifdef __WXGTK3__
        wxSize label_size = parent->GetTextExtent(label);
        wxSize def_size = wxSize(label_size.GetX() + 20, label_size.GetY());
#else
        wxSize def_size = wxDefaultSize;
#endif
        // Call Create() from wxToggleButton instead of wxBitmapToggleButton to allow add Label text under Linux
        wxToggleButton::Create(parent, id, label, wxDefaultPosition, def_size, style);
    }

#ifdef __WXMSW__
	if (parent) {
		SetBackgroundColour(parent->GetBackgroundColour());
		SetForegroundColour(parent->GetForegroundColour());
	}
#elif __WXGTK3__
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

    Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) {
	    update();

	    wxCommandEvent evt(wxEVT_CHECKBOX);
	    evt.SetInt(int(GetValue()));
	    wxPostEvent(this, evt);

	    e.Skip();
	});
}

void BitmapToggleButton::update_size()
{
#ifndef __WXGTK3__
    wxSize best_sz = GetBestSize();
    SetSize(best_sz);
#endif
}
