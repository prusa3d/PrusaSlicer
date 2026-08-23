///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationRetractionDialog.hpp"
#include "CalibrationCommon.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/GCode/CalibrationRetractionPostProcessor.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

// Number of layers printed at each retraction value
static constexpr int LAYERS_PER_LEVEL = 5;

// Height of the base plate make_retraction_towers() puts under the cylinders.
// The retraction bands span z = TOWER_BASE_HEIGHT .. TOWER_BASE_HEIGHT +
// num_levels*level_height, and make_retraction_towers() treats its argument as
// the *total* model height (base + cylinders). So the derived height must
// include the base, or the cylinders fall one base-height short of the top
// band and the highest retraction value has no geometry to print.
static constexpr double TOWER_BASE_HEIGHT = 1.0;  // mm; matches CalibrationModels.cpp

CalibrationRetractionDialog::CalibrationRetractionDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Retraction Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    // Read current retraction length from printer preset as default
    double default_retract = 0.8;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* opt = pb->printers.get_selected_preset()
                              .config.option<ConfigOptionFloats>("retract_length");
        if (opt && !opt->empty())
            default_retract = opt->get_at(0);
    }

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid  = new wxFlexGridSizer(6, 2, 10, 15);

    // Start retraction
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Start retraction (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_start_retract = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
        0.0, 10.0, std::max(0.0, default_retract - 1.0), 0.1);
    m_start_retract->SetDigits(1);
    grid->Add(m_start_retract, 0, wxEXPAND);

    // End retraction
    grid->Add(new wxStaticText(this, wxID_ANY, _L("End retraction (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_end_retract = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
        0.0, 10.0, default_retract + 1.0, 0.1);
    m_end_retract->SetDigits(1);
    grid->Add(m_end_retract, 0, wxEXPAND);

    // Retraction step
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Retraction step (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_retract_step = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
        0.01, 2.0, 0.1, 0.05);
    m_retract_step->SetDigits(2);
    grid->Add(m_retract_step, 0, wxEXPAND);

    // Tower height — computed from the retraction range, step, and layer
    // height, shown read-only. It used to be an editable field, but when set
    // below the levels-derived height it truncated the tower geometry while
    // the post-processor's band table still spanned the full height, so the
    // top retraction bands mapped above the tower and never printed.
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Tower height (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_tower_height_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
    grid->Add(m_tower_height_label, 0, wxALIGN_CENTER_VERTICAL);

    // Tower diameter
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Tower diameter (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_tower_diameter = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 3, 30, 10);
    grid->Add(m_tower_diameter, 0, wxEXPAND);

    // Tower spacing
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Tower spacing (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_tower_spacing = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 150, 34);
    grid->Add(m_tower_spacing, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_start_retract);
    wxGetApp().UpdateDarkUI(m_end_retract);
    wxGetApp().UpdateDarkUI(m_retract_step);
    wxGetApp().UpdateDarkUI(m_tower_height_label);
    wxGetApp().UpdateDarkUI(m_tower_diameter);
    wxGetApp().UpdateDarkUI(m_tower_spacing);
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

    // Keep the computed tower-height display in sync with the inputs.
    auto on_input_changed = [this](wxSpinDoubleEvent&) { update_computed_height(); };
    m_start_retract->Bind(wxEVT_SPINCTRLDOUBLE, on_input_changed);
    m_end_retract->Bind(wxEVT_SPINCTRLDOUBLE, on_input_changed);
    m_retract_step->Bind(wxEVT_SPINCTRLDOUBLE, on_input_changed);
    update_computed_height();

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (generate_and_load())
            EndModal(wxID_OK);
    }, wxID_OK);
}

// Layer height of the current print preset (falls back to 0.2 mm).
static double current_layer_height()
{
    if (const PresetBundle* pb = wxGetApp().preset_bundle) {
        if (const auto* opt = pb->prints.get_selected_preset()
                                  .config.option<ConfigOptionFloat>("layer_height"))
            return opt->getFloat();
    }
    return 0.2;
}

void CalibrationRetractionDialog::update_computed_height()
{
    if (!m_tower_height_label)
        return;

    const double start = m_start_retract->GetValue();
    const double end   = m_end_retract->GetValue();
    const double step  = m_retract_step->GetValue();

    if (end <= start || step <= 0.0) {
        m_tower_height_label->SetLabel(_L("—"));
        return;
    }

    const int    num_levels   = static_cast<int>(std::round((end - start) / step)) + 1;
    const double level_height = LAYERS_PER_LEVEL * current_layer_height();
    const double height       = TOWER_BASE_HEIGHT + num_levels * level_height;
    m_tower_height_label->SetLabel(wxString::Format(_L("%.1f mm (auto)"), height));
    Layout();
}

bool CalibrationRetractionDialog::generate_and_load()
{
    double start   = m_start_retract->GetValue();
    double end     = m_end_retract->GetValue();
    double step    = m_retract_step->GetValue();
    int    diam    = m_tower_diameter->GetValue();
    int    spacing = m_tower_spacing->GetValue();

    if (start >= end) {
        wxMessageBox(_L("End retraction must be greater than start retraction."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("Retraction step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    int num_levels = static_cast<int>(std::round((end - start) / step)) + 1;
    if (num_levels < 2) {
        wxMessageBox(_L("Retraction range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Layer height from the current print config (same source as the dialog's
    // computed tower-height display).
    double layer_height = current_layer_height();

    // Tower height is derived from the levels so every band prints in full.
    // Includes the 1 mm base (TOWER_BASE_HEIGHT) because the bands span
    // z = base .. base + num_levels*level_height and make_retraction_towers
    // takes the total model height. (Shown read-only; see update_computed_height.)
    double level_height = LAYERS_PER_LEVEL * layer_height;
    double actual_height = TOWER_BASE_HEIGHT + num_levels * level_height;

    BOOST_LOG_TRIVIAL(info) << "Generating retraction towers: start=" << start
                            << " end=" << end << " step=" << step
                            << " levels=" << num_levels
                            << " height=" << actual_height;

    auto its = Slic3r::make_retraction_towers(actual_height, double(diam), double(spacing));

    BOOST_LOG_TRIVIAL(info) << "Retraction towers: verts=" << its.vertices.size()
                            << " faces=" << its.indices.size();

    if (its.vertices.empty() || its.indices.empty()) {
        wxMessageBox(_L("Failed to generate retraction tower geometry."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Write to temp STL
    boost::filesystem::path stl_path =
        boost::filesystem::temp_directory_path() / "retraction_towers.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "retraction_towers", its)) {
        wxMessageBox(_L("Failed to write retraction tower STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load onto bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Inject job-scoped calibration markers via the model's custom per-Z
    // G-code. Two markers are emitted — one at the first layer, one at the
    // last — bracketing the tower body. They serve two purposes:
    //  1. The post_process hook below lives in the print *preset* and runs for
    //     every later slice; the post-processor only rewrites a G-code that
    //     carries this marker, so a normal print is passed through untouched.
    //     custom_gcode_per_print_z belongs to the model, so loading any other
    //     model clears the markers.
    //  2. The post-processor only rewrites retractions *between* the two
    //     markers, so retracts in the user/profile start and end G-code
    //     (nozzle priming / cleanup) are left intact.
    {
        Model& model = wxGetApp().model();
        CustomGCode::Info& cg = model.custom_gcode_per_print_z();
        cg.mode = CustomGCode::SingleExtruder;
        cg.gcodes.clear();
        const std::string marker_line =
            std::string("; ") + calibration_retraction_marker() + "\n";
        auto add_marker = [&](double z) {
            CustomGCode::Item marker;
            marker.print_z  = z;
            marker.type     = CustomGCode::Custom;
            marker.extruder = 1;
            marker.color    = "";
            marker.extra    = marker_line;
            cg.gcodes.push_back(marker);
        };
        add_marker(layer_height / 2.0);                   // start of body (first layer)
        add_marker(actual_height - layer_height / 2.0);   // end of body (last layer)
        std::sort(cg.gcodes.begin(), cg.gcodes.end());
    }

    // Reset print config and apply overrides
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        // 0% infill keeps the cylinders hollow for stringing visibility.
        // top_solid_layers is left at the preset default so the base's top
        // surface (the annulus around each cylinder where the cross-section
        // shrinks at z=1mm) is solid — otherwise the cylinder first layer
        // prints into mid air and snaps off the base.
        config.set_key_value("fill_density", new ConfigOptionPercent(0));
        // Seam = nearest, which places the seam on the inward face of each
        // tower. This is REQUIRED, not cosmetic: the seam is the perimeter
        // start/end point, and the inter-tower travel runs seam-to-seam.
        // Inward seams route that travel straight across the observation gap,
        // so stringing forms where it can be seen. spRear would route the
        // travel along the back (+Y) instead — the strings would still form,
        // just out of view, making the gap look deceptively clean.
        // The seam also accumulates a small deretract blob each layer; that
        // ridge is itself a valid retraction signal (thick at low-retract
        // bands, clean at high-retract bands), not contamination.
        config.set_key_value("seam_position",
            new ConfigOptionEnum<SeamPosition>(spNearest));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    // Read the printer's baseline retract_length so the base layers print
    // with the user's normal retraction setting, only varying inside the
    // tower section. Use the maximum test value as a slicer-side sentinel:
    // PrusaSlicer will emit explicit `G1 E-<end>` retracts at every travel,
    // and our post-process script rewrites each one based on Z.
    double base_retract  = 0.7;
    size_t num_extruders = 1;
    wxGetApp().preset_bundle->printers.discard_current_changes();
    {
        DynamicPrintConfig& printer_config =
            wxGetApp().preset_bundle->printers.get_edited_preset().config;
        const auto* opt_rl = printer_config.option<ConfigOptionFloats>("retract_length");
        if (opt_rl && !opt_rl->empty()) {
            base_retract  = opt_rl->get_at(0);
            num_extruders = std::max<size_t>(opt_rl->size(), 1);
        }

        // Buddy firmware (Core One / MK4 / MK4S / MK3.5 / MINI / iX / XL)
        // does NOT implement M207/G10/G11. Force slicer-side retraction so
        // PrusaSlicer emits real `G1 E-x` moves that the firmware actually
        // honors.
        printer_config.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));

        // Force ASCII G-code output. PrusaSlicer writes binary G-code directly
        // during export and only THEN runs post-process scripts, so our text
        // rewriter sees a sealed .bgcode and has nothing to rewrite. Buddy
        // accepts .gcode just fine.
        printer_config.set_key_value("binary_gcode", new ConfigOptionBool(false));

        // Force relative E distances. The post-processor classifies a
        // retract-only `G1 E…` move by the sign of E (negative = retract,
        // positive = recovery). That only holds in relative-E mode; with
        // absolute E a retract is just a smaller absolute coordinate and is
        // usually still positive, so it would be misread as a recovery and
        // rewritten to a tiny absolute position. Pinning relative E keeps the
        // sign-based classification valid.
        printer_config.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));

        // Force linear (non-volumetric) E. With volumetric extrusion the E
        // axis is in mm^3; the post-processor writes retraction lengths in
        // linear mm, so volumetric mode would scale every test band by the
        // filament cross-section and invalidate the calibration.
        printer_config.set_key_value("use_volumetric_e", new ConfigOptionBool(false));

        // Set retract_length to the maximum test value across every extruder
        // entry. Every travel will retract by `end` until the post-process
        // pass rewrites it.
        std::vector<double> rl_values(num_extruders, end);
        printer_config.set_key_value("retract_length", new ConfigOptionFloats(rl_values));

        // Force retract == recovery so the rewrite is symmetric.
        std::vector<double> zero_values(num_extruders, 0.0);
        printer_config.set_key_value("retract_restart_extra", new ConfigOptionFloats(zero_values));

        // Disable wipe-while-retracting. With wipe on, PrusaSlicer spreads the
        // retraction across combined `X/Y + E` moves; the post-processor only
        // rewrites pure-E retract moves, so wiped retractions would be left at
        // the sentinel retract_length and the bands would be wrong. A plain
        // retract is also the right thing to test in isolation.
        std::vector<unsigned char> wipe_off(num_extruders, 0u);
        printer_config.set_key_value("wipe", new ConfigOptionBools(wipe_off));

        wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();
    }

    // Neutralize filament-level retraction overrides. `filament_wipe`,
    // `filament_retract_length`, etc. are per-filament settings that take
    // precedence over the printer config pinned above. Prusament PETG, for
    // example, ships `filament_wipe = 1` — that re-enables wipe-while-
    // retracting, which spreads retraction across combined X/Y+E moves the
    // post-processor cannot rewrite (and the wipe move itself scrubs the
    // nozzle), so every band ends up with the same effective retraction and
    // the towers show no stringing gradient at all. Pin the filament
    // overrides to match the printer config so the printer values win.
    wxGetApp().preset_bundle->filaments.discard_current_changes();
    {
        DynamicPrintConfig& fil_config =
            wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        fil_config.set_key_value("filament_wipe",
            new ConfigOptionBoolsNullable(num_extruders, false));
        fil_config.set_key_value("filament_retract_length",
            new ConfigOptionFloatsNullable(num_extruders, end));
        fil_config.set_key_value("filament_retract_restart_extra",
            new ConfigOptionFloatsNullable(num_extruders, 0.0));
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
    }

    // --- Wire up the in-process post-processor ---
    //
    // PrusaSlicer's `post_process` config accepts a list of script paths. We
    // use a `::builtin::` URL that PostProcessor.cpp intercepts and dispatches
    // to `Slic3r::run_calibration_retraction_post_processor()` — no external
    // interpreter, no temp script file, no platform dependencies.
    std::vector<std::pair<double, double>> levels;
    levels.reserve(num_levels);
    for (int i = 0; i < num_levels; ++i) {
        double z_top   = TOWER_BASE_HEIGHT + double(i + 1) * level_height;
        double retract = start + i * step;
        levels.emplace_back(z_top, retract);
    }
    std::string builtin_url = make_calibration_retraction_url(base_retract, levels);

    {
        DynamicPrintConfig& print_config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;

        // Merge the built-in hook into any user-configured post-processing
        // scripts rather than replacing the list — overwriting it would
        // silently drop the user's scripts (delivery hooks, file transforms).
        // Strip any stale calibration hook from a previous run (idempotent),
        // then insert ours at the FRONT: run_post_process_scripts() executes
        // entries in order, so the retraction rewrite must run before any
        // user script that consumes or transforms the G-code (e.g. a step
        // that re-binarizes or uploads it).
        std::vector<std::string> scripts;
        if (const auto* pp = print_config.option<ConfigOptionStrings>("post_process"))
            scripts = pp->values;
        scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
                          [](const std::string& s) {
                              return is_calibration_retraction_url(s);
                          }),
                      scripts.end());
        scripts.insert(scripts.begin(), builtin_url);
        print_config.set_key_value("post_process", new ConfigOptionStrings(scripts));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    BOOST_LOG_TRIVIAL(info) << "Retraction calibration: post-process URL " << builtin_url
                            << ", base_retract=" << base_retract
                            << ", levels=" << num_levels;

    apply_calibration_filename_prefix("Retraction");

    // Clean up temp STL
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
