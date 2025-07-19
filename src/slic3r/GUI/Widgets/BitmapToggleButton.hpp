#ifndef slic3r_GUI_BitmapToggleButton_hpp_
#define slic3r_GUI_BitmapToggleButton_hpp_

#include <wx/tglbtn.h>
#include <stddef.h>
#include <wx/defs.h>
#include <wx/string.h>
#include <cstddef>

class wxWindow;

class BitmapToggleButton : public wxBitmapToggleButton
{
	virtual void update() = 0;

public:
	BitmapToggleButton(wxWindow * parent = NULL, const wxString& label = wxEmptyString, wxWindowID id = wxID_ANY);

protected:
	void update_size();
};

#endif // !slic3r_GUI_BitmapToggleButton_hpp_
