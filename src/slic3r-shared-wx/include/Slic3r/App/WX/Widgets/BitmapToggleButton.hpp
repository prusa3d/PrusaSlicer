#pragma once

#include <wx/tglbtn.h>

namespace Slic3r::App::WX::Widgets {

class BitmapToggleButton : public wxBitmapToggleButton
{
	virtual void update() = 0;

public:
	BitmapToggleButton(wxWindow * parent = NULL, const wxString& label = wxEmptyString, wxWindowID id = wxID_ANY);

protected:
	void update_size();
};

}


