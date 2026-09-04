///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ButtonsDescription.hpp"
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/statbmp.h>
#include <wx/clrpicker.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"
#include "BitmapCache.hpp"

namespace Slic3r {
namespace GUI {

//static ModePaletteComboBox::PalettesMap MODE_PALETTES =
static std::vector<std::pair<std::string, std::vector<std::string>>> MODE_PALETTES =
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
		Append(_(palette.first), *get_bmp(palette.second));
}

void ModePaletteComboBox::UpdateSelection(const std::vector<wxColour> &palette_in)
{
	for (size_t idx = 0; idx < MODE_PALETTES.size(); ++idx ) {
		const auto& palette = MODE_PALETTES[idx].second;

		bool is_selected = true;
		for (size_t mode = 0; mode < palette_in.size(); mode++)
			if (wxColour(palette[mode]) != palette_in[mode]) {
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

BitmapCache& ModePaletteComboBox::bitmap_cache()
{
	static BitmapCache bmps;
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
			bmps.emplace_back(get_bmp_bundle("mode", icon_height, icon_height, color));
			bmps.emplace_back(get_empty_bmp_bundle(wxOSX ? 5 : 6, icon_height));
		}
		bmp_bndl = bitmap_cache().insert_bndl(bitmap_key, bmps);
	}

	return bmp_bndl;
}

namespace GUI_Descriptions {

void FillSizerWithTextColorDescriptions(wxSizer* sizer, wxWindow* parent, wxColourPickerCtrl** sys_colour, wxColourPickerCtrl** mod_colour)
{
	wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(3, 5, 5);
	sizer->Add(grid_sizer, 0, wxEXPAND);

	auto add_color = [grid_sizer, parent](wxColourPickerCtrl** color_picker, const wxColour& color, const wxColour& def_color, wxString label_text) {
		// wrap the label_text to the max 80 characters
		if (label_text.Len() > 80) {
			size_t brack_pos = label_text.find_last_of(" ", 79);
			if (brack_pos > 0 && brack_pos < 80)
				label_text.insert(brack_pos + 1, "\n");
		}

		auto sys_label = new wxStaticText(parent, wxID_ANY, label_text);
		sys_label->SetForegroundColour(color);

		*color_picker = new wxColourPickerCtrl(parent, wxID_ANY, color);
		wxGetApp().UpdateDarkUI((*color_picker)->GetPickerCtrl(), true);
		(*color_picker)->Bind(wxEVT_COLOURPICKER_CHANGED, [color_picker, sys_label](wxCommandEvent&) {
			sys_label->SetForegroundColour((*color_picker)->GetColour());
			sys_label->Refresh();
		});

		auto btn = new ScalableButton(parent, wxID_ANY, "undo");
		btn->SetToolTip(_L("Revert color to default"));
		btn->Bind(wxEVT_BUTTON, [sys_label, color_picker, def_color](wxEvent& event) {
			(*color_picker)->SetColour(def_color);
			sys_label->SetForegroundColour(def_color);
			sys_label->Refresh();
		});
		parent->Bind(wxEVT_UPDATE_UI, [color_picker, def_color](wxUpdateUIEvent& evt) {
			evt.Enable((*color_picker)->GetColour() != def_color);
	    }, btn->GetId());

		grid_sizer->Add(*color_picker, 0, wxALIGN_CENTRE_VERTICAL);
		grid_sizer->Add(btn, 0, wxALIGN_CENTRE_VERTICAL);
		grid_sizer->Add(sys_label, 0, wxALIGN_CENTRE_VERTICAL);
	};

	add_color(sys_colour, wxGetApp().get_label_clr_sys(),	  wxGetApp().get_label_default_clr_system(),	_L("Value is the same as the system value"));
	add_color(mod_colour, wxGetApp().get_label_clr_modified(),wxGetApp().get_label_default_clr_modified(),	_L("Value was changed and is not equal to the system value or the last saved preset"));
}

void FillSizerWithModeColorDescriptions(
	wxSizer* sizer, wxWindow* parent, 
	std::vector<wxColourPickerCtrl**> clr_pickers, 
	std::vector<wxColour>& mode_palette)
{
	const int margin = em_unit(parent);

	auto palette_cb = new ModePaletteComboBox(parent);
    palette_cb->UpdateSelection(mode_palette);

	palette_cb->Bind(wxEVT_COMBOBOX, [clr_pickers, &mode_palette](wxCommandEvent& evt) {
		const int selection = evt.GetSelection();
		if (selection < 0)
			return;
		const auto& palette = MODE_PALETTES[selection];
		for (int mode = 0; mode < 3; mode++)
		    if  (*clr_pickers[mode]) {
				wxColour clr = wxColour(palette.second[mode]);
			    (*clr_pickers[mode])->SetColour(clr);
				mode_palette[mode] = clr;
		    }
	});

	wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
	h_sizer->Add(new wxStaticText(parent, wxID_ANY, _L("Default palette for mode markers") + ": "), 0, wxALIGN_CENTER_VERTICAL);
	h_sizer->Add(palette_cb, 1, wxEXPAND);

	sizer->Add(h_sizer, 0, wxEXPAND | wxBOTTOM, margin);

	wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(9, 5, 5);
	sizer->Add(grid_sizer, 0, wxEXPAND);

	const std::vector<wxString> names = { _L("Simple"), _CTX("Advanced", "Mode"), _L("Expert") };

	for (size_t mode = 0; mode < names.size(); ++mode) {
		wxColour& color = mode_palette[mode];

		wxColourPickerCtrl** color_picker = clr_pickers[mode];
		*color_picker = new wxColourPickerCtrl(parent, wxID_ANY, color);
		wxGetApp().UpdateDarkUI((*color_picker)->GetPickerCtrl(), true);

		(*color_picker)->Bind(wxEVT_COLOURPICKER_CHANGED, [color_picker, &color, palette_cb, &mode_palette](wxCommandEvent&) {
			const wxColour new_color = (*color_picker)->GetColour();
			if (new_color != color) {
				color = new_color;
				palette_cb->UpdateSelection(mode_palette);
			}
		});

		wxColour def_color = color;
		auto btn = new ScalableButton(parent, wxID_ANY, "undo");
		btn->SetToolTip(_L("Revert color"));

		btn->Bind(wxEVT_BUTTON, [color_picker, &color, def_color, palette_cb, &mode_palette](wxEvent& event) {
			color = def_color;
			(*color_picker)->SetColour(def_color);
			palette_cb->UpdateSelection(mode_palette);
		});
		parent->Bind(wxEVT_UPDATE_UI, [color_picker, def_color](wxUpdateUIEvent& evt) {
			evt.Enable((*color_picker)->GetColour() != def_color);
	    }, btn->GetId());

		grid_sizer->Add(*color_picker, 0, wxALIGN_CENTRE_VERTICAL);
		grid_sizer->Add(btn, 0, wxALIGN_CENTRE_VERTICAL);
		grid_sizer->Add(new wxStaticText(parent, wxID_ANY, names[mode]), 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 2*margin);
	}
}

Dialog::Dialog(wxWindow* parent, const std::vector<ButtonEntry> &entries) :
	wxDialog(parent, wxID_ANY, _(L("Buttons And Text Colors Description")), wxDefaultPosition, wxDefaultSize),
	m_entries(entries)
{
	wxGetApp().UpdateDarkUI(this);

	auto grid_sizer = new wxFlexGridSizer(3, 20, 20);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(grid_sizer, 0, wxEXPAND | wxALL, 20);

	// Icon description
	for (const ButtonEntry &entry : m_entries)
	{
		auto icon = new wxStaticBitmap(this, wxID_ANY, entry.bitmap->bmp());
		grid_sizer->Add(icon, -1, wxALIGN_CENTRE_VERTICAL);
		auto description = new wxStaticText(this, wxID_ANY, _(entry.symbol));
		grid_sizer->Add(description, -1, wxALIGN_CENTRE_VERTICAL);
		description = new wxStaticText(this, wxID_ANY, _(entry.explanation));
		grid_sizer->Add(description, -1, wxALIGN_CENTRE_VERTICAL);
	}

	// Text color description
	wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	GUI_Descriptions::FillSizerWithTextColorDescriptions(sizer, this, &sys_colour, &mod_colour);
	main_sizer->Add(sizer, 0, wxEXPAND | wxALL, 20);

	// Mode color markers description
	mode_palette = wxGetApp().get_mode_palette();

	wxSizer* mode_sizer = new wxBoxSizer(wxVERTICAL);
	GUI_Descriptions::FillSizerWithModeColorDescriptions(mode_sizer, this, { &simple, &advanced, &expert }, mode_palette);
	main_sizer->Add(mode_sizer, 0, wxEXPAND | wxALL, 20);

	auto buttons = CreateStdDialogButtonSizer(wxOK|wxCANCEL);
	main_sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	wxButton* btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
	btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		wxGetApp().set_label_clr_sys(sys_colour->GetColour());
		wxGetApp().set_label_clr_modified(mod_colour->GetColour());
		wxGetApp().set_mode_palette(mode_palette);

		EndModal(wxID_OK);
	});

	wxGetApp().UpdateDarkUI(btn);
	wxGetApp().UpdateDarkUI(static_cast<wxButton*>(FindWindowById(wxID_CANCEL, this)));

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
}

} // GUI_Descriptions
} // GUI
} // Slic3r

