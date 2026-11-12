#include "ModePaletteComboBox.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"

#define L(s) s //#include "I18N.hpp"
#define _(s) s //#include "I18N.hpp"

namespace Slic3r::App::Desktop {

std::vector<std::pair<std::string, std::vector<std::string>>> ModePaletteComboBox::MODE_PALETTES =
{
	{ L("Palette 1 (default)"), { "#00B000", "#FFDC00", "#E70000" } },
	{ L("Palette 2"), { "#FC766A", "#B0B8B4", "#184A45" } },
	{ L("Palette 3"), { "#567572", "#964F4C", "#696667" } },
	{ L("Palette 4"), { "#DA291C", "#56A8CB", "#53A567" } },
	{ L("Palette 5"), { "#F65058", "#FBDE44", "#28334A" } },
	{ L("Palette 6"), { "#FF3EA5", "#EDFF00", "#00A4CC" } },
	{ L("Palette 7"), { "#E95C20", "#006747", "#4F2C1D" } },
	{ L("Palette 8"), { "#D9514E", "#2A2B2D", "#2DA8D8" } }
};

ModePaletteComboBox::ModePaletteComboBox(wxWindow* parent) :
	BitmapComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY)
{
	for (const auto& palette : MODE_PALETTES)
		Append(wxString::FromUTF8(_(palette.first)), *get_bmp(palette.second));
}

void ModePaletteComboBox::UpdateSelection(const std::vector<wxColour> &palette_in)
{
	for (size_t idx = 0; idx < MODE_PALETTES.size(); ++idx ) {
		const auto& palette = MODE_PALETTES[idx].second;

		bool is_selected = true;
		for (size_t mode = 0; mode < palette_in.size(); mode++)
			if (wxColour(wxString::FromUTF8(palette[mode])) != palette_in[mode]) {
				is_selected = false;
				break;
			}
		if (is_selected) {
			Select(int(idx));
			return;
		}
	}

	Select(-1);
}

WX::BitmapCache& ModePaletteComboBox::bitmap_cache()
{
	static WX::BitmapCache bmps;
	return bmps;
}

wxBitmapBundle * ModePaletteComboBox::get_bmp(const std::vector<std::string> &palette)
{
	std::string bitmap_key;
	for (const auto& color : palette)
	    bitmap_key += color + "+";

	const int icon_height = wxOSX ? 10 : 12;

	wxBitmapBundle* bmp_bndl = bitmap_cache().find_bndl(bitmap_key);
	if (bmp_bndl == nullptr) {
		// Create the bitmap with color bars.
		std::vector<wxBitmapBundle*> bmps;
		for (const auto& color : palette) {
			bmps.emplace_back(WX::get_bmp_bundle("mode", icon_height, icon_height, color));
			bmps.emplace_back(WX::get_empty_bmp_bundle(wxOSX ? 5 : 6, icon_height));
		}
		bmp_bndl = bitmap_cache().insert_bndl(bitmap_key, bmps);
	}

	return bmp_bndl;
}
} // Slic3r

