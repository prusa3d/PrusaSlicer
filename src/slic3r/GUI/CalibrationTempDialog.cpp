///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationTempDialog.hpp"
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
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// Use shared constants from CalibrationModels.hpp for per-layer G-code Z placement.
static constexpr double BASE_HEIGHT = Slic3r::TEMP_TOWER_BASE_HEIGHT;
static constexpr double TIER_HEIGHT = Slic3r::TEMP_TOWER_TIER_HEIGHT;

CalibrationTempDialog::CalibrationTempDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Temperature Calibration Tower"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    // Get filament temperature defaults
    int default_start = 220;
    int default_end   = 180;

    const PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        const Preset& filament = preset_bundle->filaments.get_selected_preset();
        const auto* temp_opt = filament.config.option<ConfigOptionInts>("temperature");
        if (temp_opt)
            default_start = temp_opt->get_at(0);

        const auto* first_layer_opt = filament.config.option<ConfigOptionInts>("first_layer_temperature");
        if (first_layer_opt) {
            int fl_temp = first_layer_opt->get_at(0);
            if (fl_temp != default_start && fl_temp > 0)
                default_end = fl_temp;
            else
                default_end = default_start - 40;
        } else {
            default_end = default_start - 40;
        }
    }

    // Ensure end < start
    if (default_end >= default_start)
        default_end = default_start - 40;
    if (default_end < 150)
        default_end = 150;

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid  = new wxFlexGridSizer(3, 2, 10, 15);

    // Start Temperature
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Start Temperature (°C):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_start_temp = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, 150, 350, default_start);
    grid->Add(m_start_temp, 0, wxEXPAND);

    // End Temperature
    grid->Add(new wxStaticText(this, wxID_ANY, _L("End Temperature (°C):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_end_temp = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, 150, 350, default_end);
    grid->Add(m_end_temp, 0, wxEXPAND);

    // Temperature Step
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Temperature Step (°C):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_temp_step = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, wxDefaultSize,
                                 wxSP_ARROW_KEYS, 1, 50, 5);
    grid->Add(m_temp_step, 0, wxEXPAND);

    wxGetApp().UpdateDarkUI(m_start_temp);
    wxGetApp().UpdateDarkUI(m_end_temp);
    wxGetApp().UpdateDarkUI(m_temp_step);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    // OK / Cancel
    auto* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().UpdateDarkUI(FindWindowById(wxID_OK, this));
    wxGetApp().UpdateDarkUI(FindWindowById(wxID_CANCEL, this));
    sizer->Add(btns, 0, wxEXPAND | wxALL, 10);

    SetSizer(sizer);
    sizer->SetSizeHints(this);
    Fit();
    Layout();
    CenterOnParent();

    // Bind OK to generate and load
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (generate_and_load())
            EndModal(wxID_OK);
    }, wxID_OK);
}

int CalibrationTempDialog::get_start_temp() const { return m_start_temp->GetValue(); }
int CalibrationTempDialog::get_end_temp()   const { return m_end_temp->GetValue(); }
int CalibrationTempDialog::get_temp_step()  const { return m_temp_step->GetValue(); }

bool CalibrationTempDialog::generate_and_load()
{
    int start_temp = get_start_temp();
    int end_temp   = get_end_temp();
    int step       = get_temp_step();

    if (start_temp <= end_temp) {
        wxMessageBox(_L("Start temperature must be greater than end temperature."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0) {
        wxMessageBox(_L("Temperature step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    int num_tiers = (start_temp - end_temp) / step + 1;
    if (num_tiers < 2) {
        wxMessageBox(_L("Temperature range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    BOOST_LOG_TRIVIAL(info) << "Generating temperature tower: start=" << start_temp
                            << " end=" << end_temp << " step=" << step
                            << " tiers=" << num_tiers;

    // Reset print config to saved preset before loading, so any previous
    // calibration overrides (e.g. vase mode from flow specimen) are cleared.
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

    // Generate mesh natively
    auto its = Slic3r::make_temp_tower(num_tiers, start_temp, step);

    // Write to temp STL for loading via plater
    boost::filesystem::path stl_path = boost::filesystem::temp_directory_path() / "temp_tower.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "temp_tower", its)) {
        wxMessageBox(_L("Failed to write temperature tower STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load the STL onto the bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Set per-layer custom G-code for temperature changes
    Model& model = wxGetApp().model();
    auto& info = model.custom_gcode_per_print_z();
    info.mode = CustomGCode::SingleExtruder;
    info.gcodes.clear();

    // Read layer height for Z-offset calculation
    double layer_height = 0.2;
    {
        const PresetBundle* pb = wxGetApp().preset_bundle;
        if (pb) {
            const auto* opt = pb->prints.get_selected_preset()
                                  .config.option<ConfigOptionFloat>("layer_height");
            if (opt)
                layer_height = opt->getFloat();
        }
    }

    // Set the base plate temperature to match the first tier
    {
        CustomGCode::Item base_item;
        base_item.print_z  = layer_height / 2.0;  // first layer
        base_item.type     = CustomGCode::Custom;
        base_item.extruder = 1;
        base_item.color    = "";
        base_item.extra    = "M104 S" + std::to_string(start_temp) + "\n";
        info.gcodes.push_back(base_item);
    }

    for (int i = 0; i < num_tiers; ++i) {
        // Place M104 slightly below the tier boundary so the temperature
        // changes BEFORE the new tier's first layer (including cones) prints.
        double z = BASE_HEIGHT + i * TIER_HEIGHT - layer_height / 2.0;
        int temp = start_temp - i * step;

        CustomGCode::Item item;
        item.print_z  = z;
        item.type     = CustomGCode::Custom;
        item.extruder = 1;
        item.color    = "";
        item.extra    = "M104 S" + std::to_string(temp) + "\n";
        info.gcodes.push_back(item);
    }
    std::sort(info.gcodes.begin(), info.gcodes.end());

    apply_calibration_filename_prefix("TempTower");

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
