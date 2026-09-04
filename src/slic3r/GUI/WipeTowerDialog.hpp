///|/ Copyright (c) Prusa Research 2018 - 2019 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef _WIPE_TOWER_DIALOG_H_
#define _WIPE_TOWER_DIALOG_H_

#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/msgdlg.h>

#include "RammingChart.hpp"
#include "Widgets/SpinInput.hpp"


class WipingPanel : public wxPanel {
public:
    WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<std::string>& extruder_colours,
                const std::vector<double>& filament_purging_multipliers, double printer_purging_volume, wxButton* widget_button);
    std::vector<float> read_matrix_values();
	void format_sizer(wxSizer* sizer, wxPanel* page, wxGridSizer* grid_sizer, const wxString& table_title, int table_lshift=0);
        
private:        
    std::vector<std::vector<wxTextCtrl*>> edit_boxes;
    std::vector<wxColour> m_colours;
    unsigned int m_number_of_extruders  = 0;
    wxPanel*	m_page_advanced = nullptr;
    wxBoxSizer*	m_sizer = nullptr;
    wxBoxSizer* m_sizer_advanced = nullptr;
    wxGridSizer* m_gridsizer_advanced = nullptr;
    wxButton* m_widget_button     = nullptr;
    double              m_printer_purging_volume;
    std::vector<double> m_filament_purging_multipliers; // In percents !
};





class WipingDialog : public wxDialog {
public:
    WipingDialog(wxWindow* parent, const std::vector<float>& matrix, const std::vector<std::string>& extruder_colours,
                 double printer_purging_volume, const std::vector<double>& filament_purging_multipliers, bool use_custom_matrix);
    std::vector<float> get_matrix() const    { return m_output_matrix; }
    bool get_use_custom_matrix() const       { return m_radio_button2->GetValue(); }


private:
    void enable_or_disable_panel();

    WipingPanel*       m_panel_wiping  = nullptr;
    std::vector<float> m_output_matrix;
    wxRadioButton*     m_radio_button1 = nullptr;
    wxRadioButton*     m_radio_button2 = nullptr;
    wxButton*          m_widget_button = nullptr;
    wxStaticText*      m_info_text1    = nullptr;
};

#endif  // _WIPE_TOWER_DIALOG_H_