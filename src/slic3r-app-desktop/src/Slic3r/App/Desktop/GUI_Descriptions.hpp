#pragma once

#include <wx/dialog.h>
#include <vector>

class wxColourPickerCtrl;

namespace Slic3r::App {

namespace WX {
class ScalableBitmap;
}

namespace Desktop::GUI_Descriptions {

struct ButtonEntry {
	ButtonEntry(WX::ScalableBitmap *bitmap, const std::string &symbol, const std::string &explanation) : bitmap(bitmap), symbol(symbol), explanation(explanation) {}

	WX::ScalableBitmap *bitmap;
	std::string     symbol;
	std::string   	explanation;
};

class Dialog : public wxDialog
{
	std::vector<ButtonEntry> m_entries;

	wxColourPickerCtrl* sys_colour{ nullptr };
	wxColourPickerCtrl* mod_colour{ nullptr };

	wxColourPickerCtrl* simple    { nullptr };
	wxColourPickerCtrl* advanced  { nullptr };
	wxColourPickerCtrl* expert    { nullptr };

	std::vector<wxColour> mode_palette;
public:

	Dialog(wxWindow* parent, const std::vector<ButtonEntry> &entries);
	~Dialog() {}
};

extern void FillSizerWithTextColorDescriptions(wxSizer* sizer, wxWindow* parent, wxColourPickerCtrl** sys_colour, wxColourPickerCtrl** mod_colour);
extern void FillSizerWithModeColorDescriptions(wxSizer* sizer, wxWindow* parent,
		                                       std::vector<wxColourPickerCtrl**> clr_pickers,
		                                       std::vector<wxColour>& mode_palette);
} // GUI_Descriptions

} 
