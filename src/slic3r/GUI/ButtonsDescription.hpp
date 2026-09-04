///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ButtonsDescription_hpp
#define slic3r_ButtonsDescription_hpp

#include <wx/dialog.h>
#include <vector>

#include <wx/bmpbndl.h>

#include "BitmapComboBox.hpp"

class ScalableBitmap;
class wxColourPickerCtrl;

namespace Slic3r {
namespace GUI {

class BitmapCache;

// ---------------------------------
// ***  PaletteComboBox  ***
// ---------------------------------

// BitmapComboBox used to palets list in GUI Preferences
class ModePaletteComboBox : public BitmapComboBox
{
public:
    ModePaletteComboBox(wxWindow* parent);
	~ModePaletteComboBox() = default;

	void UpdateSelection(const std::vector<wxColour>& palette_in);

protected:
    // Caching bitmaps for the all bitmaps, used in preset comboboxes
    static BitmapCache&		bitmap_cache();
    wxBitmapBundle*			get_bmp( const std::vector<std::string>& palette);
};

namespace GUI_Descriptions {

struct ButtonEntry {
	ButtonEntry(ScalableBitmap *bitmap, const std::string &symbol, const std::string &explanation) : bitmap(bitmap), symbol(symbol), explanation(explanation) {}

	ScalableBitmap *bitmap;
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

} // GUI
} // Slic3r


#endif 

