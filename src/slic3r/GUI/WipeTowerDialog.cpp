///|/ Copyright (c) Prusa Research 2018 - 2022 Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <algorithm>
#include <sstream>
#include "WipeTowerDialog.hpp"
#include "BitmapCache.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "slic3r/GUI/format.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"

#include "libslic3r/Color.hpp"

#include <wx/sizer.h>

using namespace Slic3r::GUI;

int scale(const int val) { return val * wxGetApp().em_unit(); }
int ITEM_WIDTH() { return scale(6); }

static void update_ui(wxWindow* window)
{
    wxGetApp().UpdateDarkUI(window);
}

void RammingPanel::line_parameters_changed() {
    m_ramming_line_width_multiplicator = m_widget_ramming_line_width_multiplicator->GetValue();
    m_ramming_step_multiplicator = m_widget_ramming_step_multiplicator->GetValue();
}

std::string RammingPanel::get_parameters()
{
    std::vector<float> speeds = m_chart->get_ramming_speed(0.25f);
    std::vector<std::pair<float,float>> buttons = m_chart->get_buttons();
    std::stringstream stream;
    stream << m_ramming_line_width_multiplicator << " " << m_ramming_step_multiplicator;
    for (const float& speed_value : speeds)
        stream << " " << speed_value;
    stream << "|";    
    for (const auto& button : buttons)
        stream << " " << button.first << " " << button.second;
    return stream.str();
}


// Parent dialog for purging volume adjustments - it fathers WipingPanel widget (that contains all controls) and a button.
WipingDialog::WipingDialog(wxWindow* parent, const std::vector<float>& matrix, const std::vector<std::string>& extruder_colours,
                           double printer_purging_volume, const std::vector<double>& filament_purging_multipliers, bool use_custom_matrix)
    : wxDialog(parent, wxID_ANY, _(L("Wipe tower - Purging volume adjustment")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE/* | wxRESIZE_BORDER*/)
{
    SetFont(wxGetApp().normal_font());
    update_ui(this);
    m_widget_button = new wxButton(this,wxID_ANY,_L("Set values from configuration"), wxPoint(0, 0), wxDefaultSize);
    update_ui(m_widget_button);
    wxGetApp().SetWindowVariantForButton(m_widget_button);

    m_radio_button1 = new wxRadioButton(this, wxID_ANY, _L("Use values from configuration"));
    m_radio_button2 = new wxRadioButton(this, wxID_ANY, _L("Use custom project-specific settings"));
    auto stb1        = new wxStaticBox(this, wxID_ANY, wxEmptyString);
    auto stb2        = new wxStaticBox(this, wxID_ANY, wxEmptyString);

     m_panel_wiping  = new WipingPanel(this, matrix, extruder_colours, filament_purging_multipliers, printer_purging_volume, m_widget_button);

    update_ui(m_radio_button1);
    update_ui(m_radio_button2);
    update_ui(stb1);
    update_ui(stb2);

    auto heading_text = new wxStaticText(this, wxID_ANY, _L("The project uses single-extruder multimaterial printer with the wipe tower.\nThe volume of material used for purging can be configured here.") ,wxDefaultPosition, wxDefaultSize);
    m_info_text1   = new wxStaticText(this, wxID_ANY, _L("Options 'multimaterial_purging' and 'filament_purge_multiplier' will be used.") ,wxDefaultPosition, wxDefaultSize);

	// set min sizer width according to extruders count
	const auto sizer_width = (int)((std::sqrt(matrix.size()) + 2.8)*ITEM_WIDTH());
    auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->SetMinSize(wxSize(sizer_width, -1));

    main_sizer->Add(heading_text, 0, wxALL, 10);

    main_sizer->Add(m_radio_button1, 0, wxALL, 10);
    auto stb_sizer1 = new wxStaticBoxSizer(stb1, wxHORIZONTAL);
    stb_sizer1->Add(m_info_text1, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 5);
    main_sizer->Add(stb_sizer1, 0, wxALIGN_CENTER_HORIZONTAL | wxEXPAND | wxLEFT | wxRIGHT, 20);

    auto t = new wxStaticText(this, wxID_ANY, _L("(all values in mm³)"), wxDefaultPosition, wxDefaultSize);

    main_sizer->Add(m_radio_button2, 0, wxALL, 10);
    auto stb_sizer2 = new wxStaticBoxSizer(stb2, wxVERTICAL);
    stb_sizer2->Add(m_panel_wiping, 0, wxEXPAND | wxALL, 5);
    stb_sizer2->Add(t, 0, wxALIGN_CENTER_HORIZONTAL | wxCENTER | wxBOTTOM, 5);
    stb_sizer2->Add(m_widget_button, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    main_sizer->Add(stb_sizer2, 0, wxALIGN_CENTER_HORIZONTAL | wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 20);

    auto buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().SetWindowVariantForButton(buttons->GetAffirmativeButton());
    wxGetApp().SetWindowVariantForButton(buttons->GetCancelButton());
    main_sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    update_ui(static_cast<wxButton*>(this->FindWindowById(wxID_OK, this)));
    update_ui(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)));

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    
    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {                 // if OK button is clicked..
        m_output_matrix    = m_panel_wiping->read_matrix_values();    // ..query wiping panel and save returned values
        EndModal(wxID_OK);
        },wxID_OK);

    this->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent&) {
        enable_or_disable_panel();
    });

    m_radio_button1->SetValue(! use_custom_matrix);
    m_radio_button2->SetValue(use_custom_matrix);
    enable_or_disable_panel();

    this->Show();

}

// This function allows to "play" with sizrs parameters (like align or border)
void WipingPanel::format_sizer(wxSizer* sizer, wxPanel* page, wxGridSizer* grid_sizer, const wxString& table_title, int table_lshift/*=0*/)
{
	auto table_sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(table_sizer, 0, wxALIGN_CENTER | wxCENTER, table_lshift);
	table_sizer->Add(new wxStaticText(page, wxID_ANY, table_title), 0, wxALIGN_CENTER | wxTOP, 10);
	table_sizer->Add(grid_sizer, 0, wxALIGN_CENTER | wxTOP | wxLEFT, 15);
}


WipingPanel::WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<std::string>& extruder_colours,
                         const std::vector<double>& filament_purging_multipliers, double printer_purging_volume, wxButton* widget_button)
: wxPanel(parent,wxID_ANY, wxDefaultPosition, wxDefaultSize/*,wxBORDER_RAISED*/)
{
    m_filament_purging_multipliers = filament_purging_multipliers;
    m_printer_purging_volume = printer_purging_volume;
    m_widget_button = widget_button;    // pointer to the button in parent dialog
    m_widget_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){
        // Set the matrix to defaults.
        for (size_t i = 0; i < m_number_of_extruders; ++i) {
            for (size_t j = 0; j < m_number_of_extruders; ++j) {
                if (i != j) {
                    double def_val = m_printer_purging_volume * m_filament_purging_multipliers[j] / 100.;
                    edit_boxes[j][i]->SetValue(wxString("") << int(def_val));
                }
            }
        }
    });

    m_number_of_extruders = (int)(sqrt(matrix.size())+0.001);

    for (const std::string& color : extruder_colours) {
        Slic3r::ColorRGB rgb;
        Slic3r::decode_color(color, rgb);
        m_colours.push_back(wxColor(rgb.r_uchar(), rgb.g_uchar(), rgb.b_uchar()));
    }

    m_sizer_advanced        = new wxBoxSizer(wxVERTICAL);
	m_page_advanced			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_page_advanced->SetSizer(m_sizer_advanced);

    update_ui(m_page_advanced);

    m_gridsizer_advanced = new wxGridSizer(m_number_of_extruders+1, 5, 1);

	// First create controls for advanced mode and assign them to m_page_advanced:
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
		edit_boxes.push_back(std::vector<wxTextCtrl*>(0));

		for (unsigned int j = 0; j < m_number_of_extruders; ++j) {
#ifdef _WIN32
            wxTextCtrl* text = new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1), wxBORDER_SIMPLE);
            update_ui(text);
            edit_boxes.back().push_back(text);
#else
			edit_boxes.back().push_back(new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH(), -1)));
#endif
			if (i == j)
				edit_boxes[i][j]->Disable();
			else
				edit_boxes[i][j]->SetValue(wxString("") << int(matrix[m_number_of_extruders*j + i]));
		}
	}

    const int clr_icon_side = edit_boxes.front().front()->GetSize().y;
    const auto icon_size = wxSize(clr_icon_side, clr_icon_side);

	m_gridsizer_advanced->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("")));
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
        auto hsizer = new wxBoxSizer(wxHORIZONTAL);
        hsizer->AddSpacer(20);
        hsizer->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("") << i + 1), 0, wxALIGN_CENTER);
        wxWindow* w = new wxWindow(m_page_advanced, wxID_ANY, wxDefaultPosition, icon_size, wxBORDER_SIMPLE);
        w->SetCanFocus(false);
        w->SetBackgroundColour(m_colours[i]);
        hsizer->AddStretchSpacer();
        hsizer->Add(w);
		m_gridsizer_advanced->Add(hsizer, 1, wxEXPAND);
    }
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
        auto hsizer = new wxBoxSizer(wxHORIZONTAL);
        wxWindow* w = new wxWindow(m_page_advanced, wxID_ANY, wxDefaultPosition, icon_size, wxBORDER_SIMPLE);
        w->SetCanFocus(false);
        w->SetBackgroundColour(m_colours[i]);
        hsizer->AddSpacer(20);
        hsizer->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("") << i + 1), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
        hsizer->AddStretchSpacer();
        hsizer->Add(w);
        m_gridsizer_advanced->Add(hsizer, 1, wxEXPAND);

    for (unsigned int j = 0; j < m_number_of_extruders; ++j)
        m_gridsizer_advanced->Add(edit_boxes[j][i], 0);
    }

	// collect and format sizer
	format_sizer(m_sizer_advanced, m_page_advanced, m_gridsizer_advanced, _(L("Extruder changed to")));


	m_sizer = new wxBoxSizer(wxVERTICAL);
	m_sizer->Add(m_page_advanced, 0, wxEXPAND | wxALL, 5);

	m_sizer->SetSizeHints(this);
	SetSizer(m_sizer);

    m_page_advanced->Bind(wxEVT_PAINT,[this](wxPaintEvent&) {
                                              wxPaintDC dc(m_page_advanced);
                                              int y_pos = 0.5 * (edit_boxes[0][0]->GetPosition().y + edit_boxes[0][edit_boxes.size()-1]->GetPosition().y + edit_boxes[0][edit_boxes.size()-1]->GetSize().y);
                                              wxString label = _(L("From"));
                                              int text_width = 0;
                                              int text_height = 0;
                                              dc.GetTextExtent(label,&text_width,&text_height);

                                              int xpos = m_gridsizer_advanced->GetPosition().x;
                                              if (!m_page_advanced->IsEnabled()) {
                                                  dc.SetTextForeground(wxSystemSettings::GetColour(
#if defined (__linux__) && defined (__WXGTK2__)
                                                      wxSYS_COLOUR_BTNTEXT
#else 
                                                      wxSYS_COLOUR_GRAYTEXT
#endif 
                                                      ));
                                                  dc.DrawRotatedText(label, xpos - text_height, y_pos + text_width / 2.f, 90);
#ifdef _WIN32
                                                  dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT));
                                                  dc.DrawRotatedText(label, xpos - text_height-1, y_pos + text_width / 2.f+1, 90);
#endif
                                              }
                                              else
                                                  dc.DrawRotatedText(label, xpos - text_height, y_pos + text_width / 2.f, 90);
    });
}




// Reads values from the (advanced) wiping matrix:
std::vector<float> WipingPanel::read_matrix_values() {
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        for (unsigned int j=0;j<m_number_of_extruders;++j) {
            double val = 0.;
            edit_boxes[j][i]->GetValue().ToDouble(&val);
            output.push_back((float)val);
        }
    }
    return output;
}


void WipingDialog::enable_or_disable_panel()
{
    bool enable = m_radio_button2->GetValue();
    m_info_text1->Enable(! enable);
    m_widget_button->Enable(enable);
    m_panel_wiping->Enable(enable);
    m_panel_wiping->Refresh();
}