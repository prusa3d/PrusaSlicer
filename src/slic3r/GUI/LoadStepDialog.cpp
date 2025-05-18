///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Hejl @hejllukas, Pavel Mikuš @Godrak, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 Michael Kirsch
///|/ Copyright (c) 2021 Boleslaw Ciesielski
///|/ Copyright (c) 2019 John Drake @foxox
///|/
///|/ ported from lib/Slic3r/GUI/Plater.pm:
///|/ Copyright (c) Prusa Research 2016 - 2019 Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/ Copyright (c) 2017 Matthias Gazzari @qtux
///|/ Copyright (c) Slic3r 2012 - 2016 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2017 Joseph Lenox @lordofhyphens
///|/ Copyright (c) 2015 Daren Schwenke
///|/ Copyright (c) 2014 Mark Hindess
///|/ Copyright (c) 2012 Mike Sheldrake @mesheldrake
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2012 Sam Wong
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "LoadStepDialog.hpp"
#include <wx/window.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/radiobut.h>
#include <wx/slider.h>
#include <vector>
#include <utility>

#include "GUI_App.hpp"
#include "format.hpp"
#include "MsgDialog.hpp"
#include "Widgets/CheckBox.hpp"

namespace Slic3r::GUI {

static std::vector<std::pair<std::string, PrecisionParams>> default_step_import_params = {
    {"Low"      , {0.005, 1.  }},
    {"Medium"   , {0.003, 0.5 }},
    {"High"     , {0.001, 0.25}},
};

LoadStepDialog::LoadStepDialog(wxWindow* parent, const std::string& filename, double linear_precision, double angle_precision, bool multiple_loading)
        : DPIDialog(parent, wxID_ANY, format_wxstr(_L("STEP import quality (%1%)"), filename), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        m_params({ linear_precision, angle_precision })
{

#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#else
    //SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
    const wxFont& font = wxGetApp().normal_font();
    SetFont(font);

    // Call your custom function manually after constructing base
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL); // Get the sizer

    add_params(main_sizer);
        
    main_sizer->Add(new StaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, em_unit());

    wxBoxSizer* bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_remember_chb = new ::CheckBox(this, _L("Remember my choice"));

    bottom_sizer->Add(m_remember_chb, 0, wxEXPAND | wxRIGHT, 5);
    bottom_sizer->AddStretchSpacer();

    auto buttons_sizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL);

    if (multiple_loading) {
        auto apply_btn = new wxButton(this, wxID_APPLY, _L("Apply to all"));
        apply_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) {
            m_apply_to_all = true;
            EndModal(wxID_OK);
        });
        buttons_sizer->Insert(0, apply_btn, 0, wxRIGHT, 5);
    }

    bottom_sizer->Add(buttons_sizer, 0, wxEXPAND | wxLEFT, 5);
    main_sizer->Add(bottom_sizer, 0, wxEXPAND | wxALL, 10);

    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    enable_customs(!m_default);

    // Update DarkUi just for buttons
    wxGetApp().UpdateDlgDarkUI(this, true);

}

void LoadStepDialog::add_params(wxSizer* sizer)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Select requested quality of the mesh after import: ")));

    // add radio buttons for selection default parameters

    for (const auto& [name, params] : default_step_import_params) {
        wxRadioButton* radio_def = new wxRadioButton(this, wxID_ANY, format_wxstr("%1%", _(name)));
        radio_def->Bind(wxEVT_RADIOBUTTON, [params_copy = params, this](wxEvent&) {
            m_params.linear = params_copy.linear;
            m_params.angle = params_copy.angle;

            enable_customs(false);
        });
        bool is_selected = m_params.linear == params.linear && m_params.angle == params.angle;
        radio_def->SetValue(is_selected);

        m_default |= is_selected;

        main_sizer->Add(radio_def, 0, wxLEFT | wxTOP, em_unit());
    }

    // add radio buttons for set custom parameters

    wxRadioButton* radio_custom = new wxRadioButton(this, wxID_ANY, _L("Custom"));
    radio_custom->Bind(wxEVT_RADIOBUTTON, [&, this](wxEvent&) {
        enable_customs(true);
#ifdef __linux__
        this->Fit();
#endif // __linux__
        m_params.linear = string_to_double_decimal_point(m_linear_precision_val->GetValue().ToStdString());
        m_params.angle = string_to_double_decimal_point(m_angle_precision_val->GetValue().ToStdString());
    });

    main_sizer->Add(radio_custom, 0, wxLEFT | wxTOP, em_unit());
    radio_custom->SetValue(!m_default);

    long slyder_style = wxSL_HORIZONTAL | wxSL_TICKS;
    long text_ctrl_style = wxTE_PROCESS_ENTER;
#ifdef _WIN32
    text_ctrl_style |= wxBORDER_SIMPLE;
#endif

    const wxSize def_slider_size = wxSize(15 * em_unit(), wxDefaultCoord);
    const wxSize def_editor_size = wxSize(5 * em_unit(), wxDefaultCoord);

    const int hgap = 5;
    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(4, em_unit(), hgap);
    grid_sizer->SetFlexibleDirection(wxBOTH);
    grid_sizer->AddGrowableCol(1, 1);
    grid_sizer->AddGrowableRow(0, 1);
    grid_sizer->AddGrowableRow(1, 1);

    wxBoxSizer* labels_sizer = new wxBoxSizer(wxHORIZONTAL);
    {
        const wxString left_text        = _L("Lower quality");
        const int      left_text_gap    = std::max(GetTextExtent(_L("Linear precision")).x, GetTextExtent(_L("Angle precision")).x) + 4 * hgap - GetTextExtent(left_text).x * 0.5;
        const wxString right_text       = _L("Higher quality");
        const int      right_text_gap   = GetTextExtent(_L("mm")).x + def_editor_size.x + 4 * hgap - GetTextExtent(right_text).x * 0.5;
        labels_sizer->Add(new wxStaticText(this, wxID_ANY, left_text), 0, wxLEFT, left_text_gap);
        labels_sizer->Add(new wxStaticText(this, wxID_ANY, wxEmptyString), 1, wxEXPAND);
        labels_sizer->Add(new wxStaticText(this, wxID_ANY, right_text), 0, wxRIGHT, right_text_gap);
    }

    auto high_vals = std::find_if(default_step_import_params.begin(), default_step_import_params.end(), 
                                  [](const std::pair<std::string, PrecisionParams>& val) { return val.first == "High"; });
    auto low_vals = std::find_if(default_step_import_params.begin(), default_step_import_params.end(), 
                                  [](const std::pair<std::string, PrecisionParams>& val) { return val.first == "Low"; });
    assert(high_vals != default_step_import_params.end() && low_vals != default_step_import_params.end());

    m_linear_precision_sl.init(high_vals->second.linear, low_vals->second.linear, 0.001);
    m_angle_precision_sl.init(high_vals->second.angle, low_vals->second.angle, 0.01);

    auto process_value_change = [](double& precision, wxTextCtrl* text_ctrl, wxSlider* slider, const SliderHelper& sl_helper) -> void {
        wxString str_val = text_ctrl->GetValue();
        double val = string_to_double_decimal_point(str_val.ToStdString());
        precision = sl_helper.adjust_to_region(val);
        slider->SetValue(sl_helper.get_pos(precision));

        if (wxString str_precision = format_wxstr("%1%", precision); str_precision != str_val)
            text_ctrl->SetValue(str_precision);
    };

    auto tooltip = [](const SliderHelper& sl_helper) -> wxString {
        // TRN %n% contain min, max and step values respectively
        return format_wxstr(_L("Set value from the range [%1%; %2%] with %3% step"),
                            sl_helper.min_val, sl_helper.max_val, sl_helper.val_step);
    };

    // Add "Linear precision"

    m_linear_precision_slider = new wxSlider(this, wxID_ANY, m_linear_precision_sl.get_pos(m_params.linear), m_linear_precision_sl.beg_sl_pos, m_linear_precision_sl.end_sl_pos, wxDefaultPosition, def_slider_size, slyder_style);
    m_linear_precision_slider->SetTickFreq(1);
    m_linear_precision_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent e) {
        m_params.linear = m_linear_precision_sl.get_value(m_linear_precision_slider->GetValue());
        m_linear_precision_val->SetValue(format_wxstr("%1%", m_params.linear));
    });

    m_linear_precision_val = new wxTextCtrl(this, wxID_ANY, format_wxstr("%1%", m_linear_precision_sl.adjust_to_region(m_params.linear)), wxDefaultPosition, def_editor_size, text_ctrl_style);
    m_linear_precision_val->SetToolTip(tooltip(m_linear_precision_sl));

    m_linear_precision_val->Bind(wxEVT_TEXT_ENTER, [process_value_change, this](wxCommandEvent& e) {
        process_value_change(m_params.linear, m_linear_precision_val, m_linear_precision_slider, m_linear_precision_sl);
    });
    m_linear_precision_val->Bind(wxEVT_KILL_FOCUS, [process_value_change, this](wxFocusEvent& e) {
        process_value_change(m_params.linear, m_linear_precision_val, m_linear_precision_slider, m_linear_precision_sl);
        e.Skip();
    });

    grid_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Linear precision") + ": "), 0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(m_linear_precision_slider, 1, wxEXPAND);
    grid_sizer->Add(m_linear_precision_val, 0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(new wxStaticText(this, wxID_ANY, _L("mm")), 0, wxALIGN_CENTER_VERTICAL);

    // Add "Angle precision"

    m_angle_precision_slider = new wxSlider(this, wxID_ANY, m_angle_precision_sl.get_pos(m_params.angle), m_angle_precision_sl.beg_sl_pos, m_angle_precision_sl.end_sl_pos, wxDefaultPosition, def_slider_size, slyder_style);
    m_angle_precision_slider->SetTickFreq(5);
    m_angle_precision_slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent e) {
        m_params.angle = m_angle_precision_sl.get_value(m_angle_precision_slider->GetValue());
        m_angle_precision_val->SetValue(format_wxstr("%1%", m_params.angle));
    });

    m_angle_precision_val = new wxTextCtrl(this, wxID_ANY, format_wxstr("%1%", m_angle_precision_sl.adjust_to_region(m_params.angle)), wxDefaultPosition, def_editor_size, text_ctrl_style);
    m_angle_precision_val->SetToolTip(tooltip(m_angle_precision_sl));

    m_angle_precision_val->Bind(wxEVT_TEXT_ENTER, [process_value_change, this](wxCommandEvent& e) {
        process_value_change(m_params.angle, m_angle_precision_val, m_angle_precision_slider, m_angle_precision_sl);
    });
    m_angle_precision_val->Bind(wxEVT_KILL_FOCUS, [process_value_change, this](wxFocusEvent& e) {
        process_value_change(m_params.angle, m_angle_precision_val, m_angle_precision_slider, m_angle_precision_sl);
        e.Skip();
    });

    grid_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Angle precision") + ": "), 0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(m_angle_precision_slider, 1, wxEXPAND);
    grid_sizer->Add(m_angle_precision_val, 0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(new wxStaticText(this, wxID_ANY, _L("°")), 0, wxALIGN_CENTER_VERTICAL);

    m_custom_sizer = new wxBoxSizer(wxVERTICAL);
    m_custom_sizer->Add(labels_sizer, 0, wxEXPAND | wxBOTTOM | wxTOP, em_unit());
    m_custom_sizer->Add(grid_sizer, 1, wxEXPAND);

    main_sizer->Add(m_custom_sizer, 1, wxEXPAND | wxLEFT, 3 * em_unit());
    sizer->Add(main_sizer, 1, wxEXPAND | wxALL, em_unit());
}

void LoadStepDialog::enable_customs(bool enable)
{
    m_linear_precision_slider->Enable(enable);
    m_linear_precision_val->Enable(enable);
    m_angle_precision_slider->Enable(enable);
    m_angle_precision_val->Enable(enable);
}

bool LoadStepDialog::IsCheckBoxChecked()
{
    return m_remember_chb && m_remember_chb->GetValue();
}

bool LoadStepDialog::IsApplyToAllClicked()
{
    return m_apply_to_all;
}

}    // namespace Slic3r::GUI
