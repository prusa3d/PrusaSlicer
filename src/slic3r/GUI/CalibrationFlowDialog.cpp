///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationFlowDialog.hpp"
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
#include "libslic3r/Flow.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// Must match CalibrationModels.cpp default level_height parameter.
static constexpr double LEVEL_HEIGHT = 1.0;  // mm per flow level

CalibrationFlowDialog::CalibrationFlowDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Max Volumetric Flow Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    // Defaults
    double default_start = 5.0;
    double default_end   = 20.0;
    double default_step  = 0.5;

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid  = new wxFlexGridSizer(3, 2, 10, 15);

    // Start Flow Rate
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Start Flow Rate (mm³/s):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_start_flow = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxSP_ARROW_KEYS, 1.0, 50.0, default_start, 0.5);
    m_start_flow->SetDigits(1);
    grid->Add(m_start_flow, 0, wxEXPAND);

    // End Flow Rate
    grid->Add(new wxStaticText(this, wxID_ANY, _L("End Flow Rate (mm³/s):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_end_flow = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxSP_ARROW_KEYS, 1.0, 50.0, default_end, 0.5);
    m_end_flow->SetDigits(1);
    grid->Add(m_end_flow, 0, wxEXPAND);

    // Flow Step
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Flow Step (mm³/s):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_flow_step = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxSP_ARROW_KEYS, 0.5, 10.0, default_step, 0.5);
    m_flow_step->SetDigits(1);
    grid->Add(m_flow_step, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(true);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_start_flow);
    wxGetApp().UpdateDarkUI(m_end_flow);
    wxGetApp().UpdateDarkUI(m_flow_step);
    wxGetApp().UpdateDarkUI(m_brim);

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

double CalibrationFlowDialog::get_start_flow() const { return m_start_flow->GetValue(); }
double CalibrationFlowDialog::get_end_flow()   const { return m_end_flow->GetValue(); }
double CalibrationFlowDialog::get_flow_step()  const { return m_flow_step->GetValue(); }

bool CalibrationFlowDialog::generate_and_load()
{
    double start_flow = get_start_flow();
    double end_flow   = get_end_flow();
    double step       = get_flow_step();

    if (start_flow >= end_flow) {
        wxMessageBox(_L("End flow rate must be greater than start flow rate."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("Flow step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    int num_levels = static_cast<int>(std::floor((end_flow - start_flow) / step)) + 1;
    if (num_levels < 2) {
        wxMessageBox(_L("Flow range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Read nozzle diameter and layer height from current settings.
    // These determine the cross-sectional area of the extrusion, which
    // together with feed rate gives volumetric flow.
    double nozzle_diameter = 0.4;
    double layer_height    = 0.2;

    const PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        const Preset& print_preset = preset_bundle->prints.get_selected_preset();
        const auto* lh_opt = print_preset.config.option<ConfigOptionFloat>("layer_height");
        if (lh_opt)
            layer_height = lh_opt->value;

        const Preset& printer_preset = preset_bundle->printers.get_selected_preset();
        const auto* nozzle_opt = printer_preset.config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nozzle_opt && !nozzle_opt->values.empty())
            nozzle_diameter = nozzle_opt->values[0];
    }

    // Use PrusaSlicer's canonical Flow class to compute the extrusion
    // cross-sectional area (rounded rectangle model, not simple width*height).
    // This handles auto-width, percentage widths, and correct area calculation.
    double extrusion_area = 0.0;
    if (preset_bundle) {
        const Preset& print_preset2 = preset_bundle->prints.get_selected_preset();
        const auto* ew_opt = print_preset2.config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
        if (ew_opt) {
            try {
                Flow flow = Flow::new_from_config_width(
                    frPerimeter, *ew_opt,
                    float(nozzle_diameter), float(layer_height));
                extrusion_area = flow.mm3_per_mm();
            } catch (...) {
                // Fall through to fallback below
            }
        }
        if (extrusion_area <= 0.0) {
            // Auto-width: use the default perimeter width for this nozzle
            float auto_w = Flow::auto_extrusion_width(frPerimeter, float(nozzle_diameter));
            Flow flow(auto_w, float(layer_height), float(nozzle_diameter));
            extrusion_area = flow.mm3_per_mm();
        }
    }
    if (extrusion_area <= 0.0)
        extrusion_area = 0.4 * 0.2;  // ultimate fallback

    // Set perimeter_speed so that M220 S100 produces exactly start_flow.
    // speed = flow / area.  M220 then scales from S100 (start) to
    // S(end/start*100) (end), keeping the range within firmware limits.
    double base_speed = start_flow / extrusion_area;  // mm/s

    // Format floating-point values with one decimal place for G-code comments
    auto fmt = [](double v) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        return buf;
    };

    BOOST_LOG_TRIVIAL(info) << "Generating flow specimen: start=" << start_flow
                            << " end=" << end_flow << " step=" << step
                            << " levels=" << num_levels
                            << " base_speed=" << base_speed << " mm/s"
                            << " extrusion_area=" << extrusion_area;

    // Generate mesh natively
    auto its = Slic3r::make_flow_specimen(num_levels, LEVEL_HEIGHT);

    // Write to temp STL for loading via plater
    boost::filesystem::path stl_path = boost::filesystem::temp_directory_path() / "flow_specimen.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "flow_specimen", its)) {
        wxMessageBox(_L("Failed to write flow specimen STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load the STL onto the bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Reset the print config to the saved preset before applying flow-specific
    // overrides.  This ensures a clean state if a previous calibration (e.g.
    // temperature tower) modified settings, and allows the user to revert by
    // simply re-selecting the original preset from the dropdown.
    wxGetApp().preset_bundle->prints.discard_current_changes();

    // Configure print settings for vase-mode flow testing:
    // single perimeter, no solid layers, no infill, 5mm brim for adhesion.
    // Set perimeter_speed so M220 S100 = start_flow; this keeps M220 values
    // within firmware limits (typically max ~250-400%).
    {
        DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("spiral_vase", new ConfigOptionBool(true));
        config.set_key_value("bottom_solid_layers", new ConfigOptionInt(0));
        config.set_key_value("top_solid_layers", new ConfigOptionInt(0));
        config.set_key_value("perimeters", new ConfigOptionInt(1));
        config.set_key_value("fill_density", new ConfigOptionPercent(0));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        config.set_key_value("perimeter_speed", new ConfigOptionFloat(base_speed));
        config.set_key_value("external_perimeter_speed", new ConfigOptionFloatOrPercent(base_speed, false));
        config.set_key_value("small_perimeter_speed", new ConfigOptionFloatOrPercent(base_speed, false));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    // Insert per-layer custom G-code to vary the feed rate using M220.
    // Since perimeter_speed is set so M220 S100 = start_flow, we scale
    // relative to start_flow:  percent = (target_flow / start_flow) × 100.
    // For 5→20 mm³/s this gives M220 S100 to S400.
    Model& model = wxGetApp().model();
    auto& info = model.custom_gcode_per_print_z();
    info.mode = CustomGCode::SingleExtruder;
    info.gcodes.clear();

    for (int i = 0; i < num_levels; ++i) {
        double z = i * LEVEL_HEIGHT + layer_height / 2.0;
        double target_flow = start_flow + i * step;
        int speed_percent = static_cast<int>(std::round(target_flow / start_flow * 100.0));

        CustomGCode::Item item;
        item.print_z  = z;
        item.type     = CustomGCode::Custom;
        item.extruder = 1;
        item.color    = "";
        item.extra    = "M220 S" + std::to_string(speed_percent)
                      + " ; " + fmt(target_flow) + " mm3/s\n";
        info.gcodes.push_back(item);
    }
    std::sort(info.gcodes.begin(), info.gcodes.end());

    apply_calibration_filename_prefix("MaxFlow");

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
