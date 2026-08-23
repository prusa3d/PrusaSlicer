///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationFanDialog.hpp"
#include "CalibrationCommon.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace Slic3r { namespace GUI {

CalibrationFanDialog::CalibrationFanDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Fan Speed Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid  = new wxFlexGridSizer(3, 2, 10, 15);

    // Start fan %
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Start fan speed (%):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_start_fan = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100, 0);
    grid->Add(m_start_fan, 0, wxEXPAND);

    // End fan %
    grid->Add(new wxStaticText(this, wxID_ANY, _L("End fan speed (%):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_end_fan = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 100, 100);
    grid->Add(m_end_fan, 0, wxEXPAND);

    // Step size
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Fan speed step (%):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_fan_step = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 50, 10);
    grid->Add(m_fan_step, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_start_fan);
    wxGetApp().UpdateDarkUI(m_end_fan);
    wxGetApp().UpdateDarkUI(m_fan_step);
    wxGetApp().UpdateDarkUI(m_brim);

    auto* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().UpdateDarkUI(FindWindowById(wxID_OK, this));
    wxGetApp().UpdateDarkUI(FindWindowById(wxID_CANCEL, this));
    sizer->Add(btns, 0, wxEXPAND | wxALL, 10);

    SetSizer(sizer);
    sizer->SetSizeHints(this);
    Fit();
    Layout();
    CenterOnParent();

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (generate_and_load())
            EndModal(wxID_OK);
    }, wxID_OK);
}

bool CalibrationFanDialog::generate_and_load()
{
    int start = m_start_fan->GetValue();
    int end   = m_end_fan->GetValue();
    int step  = m_fan_step->GetValue();

    if (start >= end) {
        wxMessageBox(_L("End fan speed must be greater than start fan speed."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0) {
        wxMessageBox(_L("Fan speed step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    int num_levels = (end - start) / step + 1;
    if (num_levels < 2) {
        wxMessageBox(_L("Fan speed range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Get layer height
    double layer_height = 0.2;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* opt = pb->prints.get_selected_preset()
                              .config.option<ConfigOptionFloat>("layer_height");
        if (opt)
            layer_height = opt->getFloat();
    }

    BOOST_LOG_TRIVIAL(info) << "Fan tower: start=" << start << "% end=" << end
                            << "% step=" << step << "% levels=" << num_levels;

    // Generate mesh natively
    auto its = Slic3r::make_fan_tower(num_levels);

    if (its.vertices.empty()) {
        wxMessageBox(_L("Failed to generate fan tower geometry."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Write to temp STL
    boost::filesystem::path stl_path =
        boost::filesystem::temp_directory_path() / "fan_tower.stl";
    std::string stl_str = stl_path.string();
    if (!its_write_stl_binary(stl_str.c_str(), "fan_tower", its)) {
        wxMessageBox(_L("Failed to write fan tower STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Reset and configure print settings
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    // Disable ALL automatic fan control on the FILAMENT preset so our
    // per-layer M106 commands are the sole fan speed control in the output.
    // These settings live in the filament config, not the print config.
    {
        DynamicPrintConfig& fil_config =
            wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        fil_config.set_key_value("cooling", new ConfigOptionBools({false}));
        fil_config.set_key_value("fan_always_on", new ConfigOptionBools({false}));
        fil_config.set_key_value("bridge_fan_speed", new ConfigOptionInts({-1}));
        fil_config.set_key_value("disable_fan_first_layers", new ConfigOptionInts({0}));
        fil_config.set_key_value("full_fan_speed_layer", new ConfigOptionInts({0}));
        fil_config.set_key_value("enable_dynamic_fan_speeds", new ConfigOptionBools({false}));
        fil_config.set_key_value("min_fan_speed", new ConfigOptionInts({0}));
        fil_config.set_key_value("max_fan_speed", new ConfigOptionInts({0}));
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
    }

    // Insert per-level fan speed commands (M106 S<0-255>)
    Model& model = wxGetApp().model();
    auto& info = model.custom_gcode_per_print_z();
    info.mode = CustomGCode::SingleExtruder;
    info.gcodes.clear();

    for (int i = 0; i < num_levels; ++i) {
        // Place the custom gcode at each level boundary.
        // Level 0 starts at the very first layer (Z = first layer height).
        // Subsequent levels at Z = FAN_BASE_H (1.0) + i * level_height.
        static constexpr double FAN_BASE_H = 1.0; // must match CalibrationModels.cpp
        double z = (i == 0) ? layer_height / 2.0 : FAN_BASE_H + i * FAN_TOWER_LEVEL_HEIGHT + layer_height / 2.0;
        int fan_pct = start + i * step;
        int fan_pwm = std::min(255, static_cast<int>(std::round(fan_pct * 255.0 / 100.0)));

        CustomGCode::Item item;
        item.print_z  = z;
        item.type     = CustomGCode::Custom;
        item.extruder = 1;
        item.color    = "";
        item.extra    = "M106 S" + std::to_string(fan_pwm)
                      + " ; fan " + std::to_string(fan_pct) + "%\n";
        info.gcodes.push_back(item);
    }
    std::sort(info.gcodes.begin(), info.gcodes.end());

    BOOST_LOG_TRIVIAL(info) << "Fan tower: inserted " << info.gcodes.size()
                            << " custom gcode entries";

    apply_calibration_filename_prefix("FanSpeed");

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
