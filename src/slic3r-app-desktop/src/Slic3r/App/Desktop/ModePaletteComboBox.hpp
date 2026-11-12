#pragma once

#include <vector>
#include <wx/colour.h>

#include "Slic3r/App/WX/Widgets/BitmapComboBox.hpp"
#include "Slic3r/App/WX/BitmapCache.hpp"

class wxBitmapBundle;

namespace Slic3r::App::Desktop {

// ---------------------------------
// ***  PaletteComboBox  ***
// ---------------------------------

// BitmapComboBox used to palets list in GUI Preferences
class ModePaletteComboBox : public WX::Widgets::BitmapComboBox
{
public:

    static std::vector<std::pair<std::string, std::vector<std::string>>> MODE_PALETTES;

    ModePaletteComboBox(wxWindow* parent);
	~ModePaletteComboBox() = default;

	void UpdateSelection(const std::vector<wxColour>& palette_in);

protected:
    // Caching bitmaps for the all bitmaps, used in preset comboboxes
    static WX::BitmapCache&	bitmap_cache();
    wxBitmapBundle*			get_bmp( const std::vector<std::string>& palette);
};
} // Slic3r

