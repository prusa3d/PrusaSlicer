///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPADialog.hpp"
#include "CalibrationCommon.hpp"
#include "GUI_App.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/GCode/CalibrationPAPostProcessor.hpp"
#include "libslic3r/GCode/CalibrationPALinePostProcessor.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// Number of printed layers per PA level
static constexpr int PA_LAYERS_PER_LEVEL = 4;

CalibrationPADialog::CalibrationPADialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Pressure Advance Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid  = new wxFlexGridSizer(5, 2, 10, 15);

    // Test style
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Test style:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_mode = new wxChoice(this, wxID_ANY);
    m_mode->Append(_L("Chevron tower (per-layer PA)"));
    m_mode->Append(_L("Line — K-factor speed test (flat)"));
    m_mode->SetSelection(0);
    grid->Add(m_mode, 0, wxEXPAND);

    // Start PA
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Start PA:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_start_pa = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxSP_ARROW_KEYS, 0.0, 2.0, 0.0, 0.005);
    m_start_pa->SetDigits(3);
    grid->Add(m_start_pa, 0, wxEXPAND);

    // End PA
    grid->Add(new wxStaticText(this, wxID_ANY, _L("End PA:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_end_pa = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxSP_ARROW_KEYS, 0.0, 2.0, 0.1, 0.005);
    m_end_pa->SetDigits(3);
    grid->Add(m_end_pa, 0, wxEXPAND);

    // PA Step
    grid->Add(new wxStaticText(this, wxID_ANY, _L("PA Step:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pa_step = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxDefaultSize,
                                     wxSP_ARROW_KEYS, 0.001, 0.5, 0.005, 0.001);
    m_pa_step->SetDigits(3);
    grid->Add(m_pa_step, 0, wxEXPAND);

    // Test print speed (mm/s).
    // Pressure-advance differences only manifest visibly at print speeds
    // ≳ 80 mm/s — the corner pressure spike scales with extrusion rate.
    // PrusaSlicer's default profiles print perimeters around 40–50 mm/s,
    // and on top of that the filament cooling logic slows fast-printing
    // layers further to give them time to cool. The net effect is that
    // every PA value produces an indistinguishably blurry corner.
    // Forcing 100 mm/s for the test, plus disabling cooling slowdown,
    // surfaces the actual sweet spot.
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Test Speed (mm/s):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_test_speed = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxSP_ARROW_KEYS, 30.0, 300.0, 100.0, 5.0);
    m_test_speed->SetDigits(0);
    grid->Add(m_test_speed, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_mode);
    wxGetApp().UpdateDarkUI(m_start_pa);
    wxGetApp().UpdateDarkUI(m_end_pa);
    wxGetApp().UpdateDarkUI(m_pa_step);
    wxGetApp().UpdateDarkUI(m_test_speed);
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

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (generate_and_load())
            EndModal(wxID_OK);
    }, wxID_OK);
}

bool CalibrationPADialog::generate_and_load()
{
    const int mode = m_mode ? m_mode->GetSelection() : 0;
    // Any non-Line setup disarms a stale "export the G-code to view" reminder left
    // armed by a previous Line run (the Line path re-arms it itself).
    if (mode != 1)
        if (Plater* p = wxGetApp().plater()) p->set_pa_line_export_reminder(false);
    if (mode == 0) return generate_tower();
    return generate_line_pattern();   // mode 1
}

bool CalibrationPADialog::generate_tower()
{
    double start_pa = m_start_pa->GetValue();
    double end_pa   = m_end_pa->GetValue();
    double step     = m_pa_step->GetValue();

    if (start_pa >= end_pa) {
        wxMessageBox(_L("End PA must be greater than start PA."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("PA step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    int num_levels = static_cast<int>(std::floor((end_pa - start_pa) / step)) + 1;
    if (num_levels < 2) {
        wxMessageBox(_L("PA range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Read layer height from current print preset
    double layer_height = 0.2;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* opt = pb->prints.get_selected_preset()
                              .config.option<ConfigOptionFloat>("layer_height");
        if (opt)
            layer_height = opt->getFloat();
    }

    // Total layers = levels × layers_per_level
    int total_layers = num_levels * PA_LAYERS_PER_LEVEL;

    // Format PA values for G-code comments
    auto fmt = [](double v) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    };

    BOOST_LOG_TRIVIAL(info) << "Generating PA pattern: start=" << start_pa
                            << " end=" << end_pa << " step=" << step
                            << " levels=" << num_levels
                            << " total_layers=" << total_layers;

    // Generate single-chevron mesh
    auto its = Slic3r::make_pa_pattern(total_layers, layer_height);

    // Write to temp STL
    boost::filesystem::path stl_path =
        boost::filesystem::temp_directory_path() / "pa_pattern.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "pa_pattern", its)) {
        wxMessageBox(_L("Failed to write PA pattern STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load onto bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Reset BOTH print and filament configs to their saved state so that
    // the calibration overrides we're about to apply are deterministic
    // (don't stack on top of previous calibration runs or unrelated
    // user edits) AND so the modifications are visibly marked on the
    // presets as "changed since saved" — letting the user revert with
    // a single click on the preset's ⟲ button after calibration.
    wxGetApp().preset_bundle->prints.discard_current_changes();
    wxGetApp().preset_bundle->filaments.discard_current_changes();

    const double test_speed = m_test_speed ? m_test_speed->GetValue() : 100.0;

    // Configure for PA testing: ensure consistent layer height + uniform
    // high speed so PA differences are actually visible. The chevron is
    // mostly single-wall perimeter, so we override every perimeter speed
    // to the test value; infill speeds matter less but we set them too
    // for the inner walls that bridge the chevron tips.
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        config.set_key_value("perimeter_speed",            new ConfigOptionFloat(test_speed));
        config.set_key_value("external_perimeter_speed",   new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("small_perimeter_speed",      new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("infill_speed",               new ConfigOptionFloat(test_speed));
        config.set_key_value("solid_infill_speed",         new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("top_solid_infill_speed",     new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("gap_fill_speed",             new ConfigOptionFloat(test_speed));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    // Disable cooling-driven slowdown on the FILAMENT preset and pin the
    // slowdown floor at the requested test speed. Without this, the
    // chevron's small layers (each one prints in ~1–3 s) would trigger
    // PrusaSlicer's "slow down if layer print time < N seconds" logic and
    // every PA value would homogenize to the cooling-imposed min speed,
    // hiding the differences we're trying to surface.
    {
        DynamicPrintConfig& fil_config =
            wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        fil_config.set_key_value("slowdown_below_layer_time",
                                 new ConfigOptionInts({0}));
        fil_config.set_key_value("min_print_speed",
                                 new ConfigOptionFloats({test_speed}));
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
    }

    // Pick the firmware PA command from flavor + printer model (see select_pa_command:
    // Prusa's Buddy input-shaper printers, incl. the Core One, are Marlin-flavored but
    // use M572 S, not M900 K).
    GCodeFlavor flavor = gcfRepRapFirmware;
    std::string printer_notes;
    if (pb) {
        if (const auto* fo = pb->printers.get_selected_preset()
                                 .config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor"))
            flavor = fo->value;
        if (const auto* no = pb->printers.get_selected_preset()
                                 .config.option<ConfigOptionString>("printer_notes"))
            printer_notes = no->value;
    }
    const PACalibrationCommand pa_cmd_kind = select_pa_command(flavor, printer_notes);

    auto make_pa_gcode = [&](double pa_val) -> std::string {
        std::string val_str = fmt(pa_val);
        switch (pa_cmd_kind) {
        case PACalibrationCommand::Klipper: return "SET_PRESSURE_ADVANCE ADVANCE=" + val_str + "\n";
        case PACalibrationCommand::M900:    return "M900 K" + val_str + "\n";
        default:                            return "M572 S" + val_str + "\n";
        }
    };

    // Insert per-layer PA commands.
    // Each level spans PA_LAYERS_PER_LEVEL layers.
    Model& model = wxGetApp().model();
    auto& info = model.custom_gcode_per_print_z();
    info.mode = CustomGCode::SingleExtruder;
    info.gcodes.clear();

    for (int i = 0; i < num_levels; ++i) {
        double z = i * PA_LAYERS_PER_LEVEL * layer_height + layer_height / 2.0;
        double pa = start_pa + i * step;

        CustomGCode::Item item;
        item.print_z  = z;
        item.type     = CustomGCode::Custom;
        item.extruder = 1;
        item.color    = "";
        item.extra    = make_pa_gcode(pa);
        info.gcodes.push_back(item);
    }

    // Reset PA to 0 after the last level so subsequent prints aren't affected
    {
        double z_top = (num_levels - 1) * PA_LAYERS_PER_LEVEL * layer_height
                     + PA_LAYERS_PER_LEVEL * layer_height - layer_height / 2.0;
        CustomGCode::Item reset;
        reset.print_z  = z_top;
        reset.type     = CustomGCode::Custom;
        reset.extruder = 1;
        reset.color    = "";
        reset.extra    = make_pa_gcode(0.0);
        info.gcodes.push_back(reset);
    }

    std::sort(info.gcodes.begin(), info.gcodes.end());

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    // Surface the temporary preset overrides so the user knows to revert
    // before slicing other (non-calibration) models. Both presets carry
    // changes since their saved state (the Print preset tab and Filament
    // preset tab will both show the ⟲ revert affordance), but a banner
    // makes it obvious what was changed and why.
    if (auto* nm = wxGetApp().notification_manager()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "PA calibration applied temporary overrides:\n"
            "  Print preset — perimeter / infill / gap-fill speeds → %.0f mm/s\n"
            "  Filament preset — cooling slowdown disabled, min_print_speed → %.0f mm/s\n"
            "Revert via the ⟲ buttons on the Print and Filament preset tabs "
            "before slicing other models.",
            test_speed, test_speed);
        nm->push_notification(
            NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::WarningNotificationLevel,
            buf);
    }

    apply_calibration_filename_prefix("PressureAdvance");

    return true;
}

// ---------------------------------------------------------------------------
// PA Line test (garethky / K-factor) — generated toolpath, injected on export.
// ---------------------------------------------------------------------------
// The line method is a DELIBERATE toolpath, not a sliceable shape: an anchor
// frame (left + right bars) plus one constant-Y pass per PA value, each printed
// slow→fast→slow with the firmware PA command set just before it, all welded
// into one peelable piece (lift the whole test off the plate by a bar). We build
// that toolpath here and splice it over a sliced placeholder's body via
// CalibrationPALinePostProcessor.
bool CalibrationPADialog::generate_line_pattern()
{
    const double start_pa = m_start_pa->GetValue();
    const double end_pa   = m_end_pa->GetValue();
    const double step     = m_pa_step->GetValue();
    if (start_pa >= end_pa) {
        wxMessageBox(_L("End PA must be greater than start PA."), _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("PA step must be positive."), _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    const int num_lines = static_cast<int>(std::floor((end_pa - start_pa) / step + 1e-9)) + 1;
    if (num_lines < 2) {
        wxMessageBox(_L("PA range too small for the given step."), _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (!pb) return false;
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    auto cfg_float = [](const Preset& p, const char* key, double dflt) -> double {
        if (const auto* o = p.config.option<ConfigOptionFloat>(key)) return o->value;
        return dflt;
    };
    auto cfg_floats_at = [](const Preset& p, const char* key, size_t idx, double dflt) -> double {
        if (const auto* o = p.config.option<ConfigOptionFloats>(key); o != nullptr && !o->empty())
            return o->get_at(std::min(idx, o->size() - 1));
        return dflt;
    };
    // Read the EDITED presets (not get_selected_preset) so unsaved UI changes — e.g. a
    // Flow Ratio extrusion_multiplier or a printer Z/retraction tweak not yet saved —
    // feed the generated body, matching what the export actually uses.
    const Preset& print_p   = pb->prints.get_edited_preset();
    const Preset& printer_p = pb->printers.get_edited_preset();

    // Which tool actually prints the placeholder. The printer preset's nozzle and
    // retraction options are per-extruder vectors, so on a toolchanger (INDX/XL/MMU)
    // index 0 is the wrong tool: the sweep would print at the wrong E-rate with
    // retracts that don't balance (#49).
    size_t n_extruders = 1;
    if (const auto* nd = printer_p.config.option<ConfigOptionFloats>("nozzle_diameter");
        nd != nullptr && !nd->empty())
        n_extruders = nd->size();
    // Sampled from the EDITED preset, then re-applied below after
    // prints.discard_current_changes() — otherwise an unsaved perimeter_extruder edit
    // would be sampled here but thrown away before slicing, generating the body for one
    // tool while the export used another.
    size_t extruder_idx = 0;
    if (const auto* pe = print_p.config.option<ConfigOptionInt>("perimeter_extruder"))
        extruder_idx = size_t(std::max(1, pe->value) - 1);   // 1-based in the config
    extruder_idx = std::min(extruder_idx, n_extruders - 1);

    // ...and that tool's filament. Prefer the edited filament preset when it IS this
    // extruder's filament, so unsaved Filaments-tab edits still feed the body.
    const Preset* fil_sel  = extruder_idx < pb->extruders_filaments.size()
                           ? pb->extruders_filaments[extruder_idx].get_selected_preset()
                           : nullptr;
    const Preset& fil_edit = pb->filaments.get_edited_preset();
    const Preset& fil_p    = (fil_sel == nullptr || fil_sel->name == fil_edit.name) ? fil_edit
                                                                                   : *fil_sel;

    const double lh            = cfg_float(print_p, "layer_height", 0.2);
    const double nozzle_d      = cfg_floats_at(printer_p, "nozzle_diameter", extruder_idx, 0.4);
    // A filament preset's own vectors describe that one filament, so index 0 is right here.
    const double filament_d    = cfg_floats_at(fil_p, "filament_diameter", 0, 1.75);
    const double retract_len   = cfg_floats_at(printer_p, "retract_length", extruder_idx, 0.8);
    const double retract_spd   = cfg_floats_at(printer_p, "retract_speed", extruder_idx, 40.0);
    double       deretract_spd = cfg_floats_at(printer_p, "deretract_speed", extruder_idx, 0.0);
    if (deretract_spd <= 0.0) deretract_spd = retract_spd;
    // The body emits its own retract/unretract; mirror the slicer by adding the
    // filament's de-prime (retract_restart_extra) to each unretract, and apply the
    // configured z_offset to every literal Z (the body bypasses the slicer's Z bake).
    const double restart_extra = cfg_floats_at(printer_p, "retract_restart_extra", extruder_idx, 0.0);
    const double z_offset      = cfg_float(printer_p, "z_offset", 0.0);
    if (lh <= 0.0 || nozzle_d <= 0.0 || filament_d <= 0.0) return false;

    const double test_speed = m_test_speed ? m_test_speed->GetValue() : 100.0;
    double       fast_mm_s  = test_speed;
    const double slow_mm_s  = std::min(45.0, test_speed * 0.5);

    // E per mm for a single ~nozzle-width bead at this layer height (relative E).
    // Include the filament's extrusion multiplier: the generated body bypasses the
    // slicer's Extruder::extrusion_multiplier() scaling, so on a flow-calibrated
    // filament (Flow Ratio is run before PA) this test would otherwise print at
    // uncalibrated flow and skew the result.
    const double extr_mult = cfg_floats_at(fil_p, "extrusion_multiplier", 0, 1.0);
    const double fil_area  = M_PI * (filament_d * 0.5) * (filament_d * 0.5);
    const double e_rate    = (nozzle_d * lh) / fil_area * extr_mult;

    // Keep the fast pass within the filament's volumetric limit, else the hotend
    // can't melt fast enough on exactly the fast segment the PA result is read from.
    const double max_vol_speed = cfg_floats_at(fil_p, "filament_max_volumetric_speed", 0, 0.0);
    if (max_vol_speed > 0.0) {
        const double mm3_per_mm = nozzle_d * lh * extr_mult;
        if (mm3_per_mm > 0.0)
            fast_mm_s = std::max(slow_mm_s, std::min(fast_mm_s, max_vol_speed / mm3_per_mm));
    }

    // --- Firmware PA command (see select_pa_command) ---
    GCodeFlavor flavor = gcfRepRapFirmware;
    std::string printer_notes;
    if (const auto* fo = printer_p.config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor"))
        flavor = fo->value;
    if (const auto* no = printer_p.config.option<ConfigOptionString>("printer_notes"))
        printer_notes = no->value;
    const PACalibrationCommand cmd = select_pa_command(flavor, printer_notes);
    auto pa_cmd = [&](double pa) {
        std::ostringstream s; s.imbue(std::locale::classic());
        s << std::fixed << std::setprecision(4);
        switch (cmd) {
        case PACalibrationCommand::Klipper: s << "SET_PRESSURE_ADVANCE ADVANCE=" << pa; break;
        case PACalibrationCommand::M900:    s << "M900 K" << pa; break;
        default:                            s << "M572 S" << pa; break;
        }
        return s.str();
    };

    if (plater->build_volume().type() != BuildVolume::Type::Rectangle) {
        wxMessageBox(_L("The flat PA line test needs a rectangular bed. "
                        "Use the Chevron tower style on this printer."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // --- Pattern geometry, centred on the bed ---
    const double slow_len = 25.0, fast_len = 100.0, end_len = 25.0;
    const double line_len = slow_len + fast_len + end_len;   // 150 mm
    const double spacing  = 4.0;
    const double bead     = std::max(0.6, nozzle_d);
    const double tick_len = 8.0, tick_gap = 3.0;

    const BoundingBoxf bed   = plater->build_volume().bounding_volume2d();
    const Vec2d        bed_c = bed.center();
    const double col_h  = (num_lines - 1) * spacing;
    const double need_x = line_len + bead + 4.0 + 26.0;   // +26 for the right-hand number labels
    const double need_y = col_h + 4.0 + tick_gap + tick_len + 4.0;
    if (need_x > bed.size().x() - 10.0 || need_y > bed.size().y() - 10.0) {
        wxMessageBox(wxString::Format(
            _L("This PA sweep (%d lines) does not fit on the bed. Use a larger PA "
               "step (fewer lines) or a narrower PA range."), num_lines),
            _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    const double xL  = bed_c.x() - line_len / 2.0;
    const double xR  = xL + line_len;
    const double xB1 = xL + slow_len;             // slow→fast boundary (tick)
    const double xB2 = xL + slow_len + fast_len;  // fast→slow boundary (tick)
    const double y0  = bed_c.y() - col_h / 2.0;   // start-PA line at the front
    const double bar_yB = y0 - 2.0;
    const double bar_yT = y0 + col_h + 2.0;
    const double tick_yB = bar_yT + tick_gap;
    const double tick_yT = tick_yB + tick_len;

    // --- Build the toolpath (relative E) ---
    auto fc = [](double v){ std::ostringstream s; s.imbue(std::locale::classic()); s<<std::fixed<<std::setprecision(3)<<v; return s.str(); };
    auto fe = [](double v){ std::ostringstream s; s.imbue(std::locale::classic()); s<<std::fixed<<std::setprecision(5)<<v; return s.str(); };
    auto fz = [](double v){ std::ostringstream s; s.imbue(std::locale::classic()); s<<std::fixed<<std::setprecision(2)<<v; return s.str(); };
    const long slowF  = std::lround(slow_mm_s * 60.0);
    const long fastF  = std::lround(fast_mm_s * 60.0);
    const long travF  = 21000;
    const long retF   = std::lround(retract_spd * 60.0);
    const long deretF = std::lround(deretract_spd * 60.0);
    const double zhi  = lh + 0.2 + z_offset;

    std::ostringstream tp; tp.imbue(std::locale::classic());
    // Flavour-correct acceleration change (mirrors GCodeWriter::set_acceleration_internal):
    // M204 P/T for Marlin-firmware / RepRapFirmware, M201/M202 for Repetier, and M204 S
    // elsewhere (Marlin legacy, Klipper, Smoothie...). A hard-coded "M204 P.. T.." is
    // silently ignored or rejected on those other flavours, printing the test with the
    // wrong motion settings.
    auto accel = [&](long print_a, long travel_a) {
        switch (flavor) {
        case gcfRepetier:
            tp << "M201 X" << print_a << " Y" << print_a << "\n";
            tp << "M202 X" << travel_a << " Y" << travel_a << "\n";
            break;
        case gcfRepRapFirmware:
        case gcfMarlinFirmware:
            tp << "M204 P" << print_a << " T" << travel_a << "\n";
            break;
        default:
            tp << "M204 S" << print_a << "\n";
            break;
        }
    };
    auto travel = [&](double x, double y) {
        tp << "G1 Z" << fz(zhi) << " F720 ; lift\n";
        accel(7000, 7000);
        tp << "G1 X" << fc(x) << " Y" << fc(y) << " F" << travF << " ; travel move\n";
        accel(500, 500);
        tp << "G1 Z" << fz(lh + z_offset) << " F720 ; lower\n";
    };
    auto unretract = [&]{ tp << "G1 E" << fe(retract_len + restart_extra) << " F" << deretF << " ; un-retract\n"; };
    auto do_retract= [&]{ tp << "G1 E-" << fe(retract_len) << " F" << retF << " ; retract\n"; };
    auto seg = [&](double x, double y, double len, long f) {
        tp << "G1 X" << fc(x) << " Y" << fc(y) << " E" << fe(len * e_rate) << " F" << f << " ; print line\n";
    };

    // --- Number-label glyphs (drawn as strokes in the clear area right of the
    // pattern). A flat travel (no Z-hop) is fine there since nothing is in the
    // way; one Z-hop carries the nozzle over the pattern into the label column. ---
    auto fmt3 = [](double v){ std::ostringstream s; s.imbue(std::locale::classic()); s<<std::fixed<<std::setprecision(3)<<v; return s.str(); };
    auto travel_flat = [&](double x, double y){
        tp << "G1 X" << fc(x) << " Y" << fc(y) << " F" << travF << " ; travel\n";
    };
    auto stroke = [&](double x0, double y0, double x1, double y1){
        travel_flat(x0, y0);
        unretract();
        seg(x1, y1, std::hypot(x1 - x0, y1 - y0), slowF);
        do_retract();
    };
    // Minimal 7-segment glyph for digits and '.', origin at the bottom-left.
    auto glyph = [&](char ch, double ox, double oy, double cw, double cht){
        const double xm = ox + cw, ym = oy + cht, yh = oy + cht / 2.0;
        auto A=[&]{stroke(ox,ym,xm,ym);}; auto B=[&]{stroke(xm,ym,xm,yh);};
        auto C=[&]{stroke(xm,yh,xm,oy);}; auto D=[&]{stroke(ox,oy,xm,oy);};
        auto E=[&]{stroke(ox,yh,ox,oy);}; auto F=[&]{stroke(ox,ym,ox,yh);};
        auto G=[&]{stroke(ox,yh,xm,yh);};
        switch (ch) {
        case '0': A();B();C();D();E();F();break;
        case '1': B();C();break;
        case '2': A();B();G();E();D();break;
        case '3': A();B();C();D();G();break;
        case '4': F();G();B();C();break;
        case '5': A();F();G();C();D();break;
        case '6': A();F();G();E();D();C();break;
        case '7': A();B();C();break;
        case '8': A();B();C();D();E();F();G();break;
        case '9': A();B();C();D();F();G();break;
        case '.': stroke(ox+cw/2.0, oy, ox+cw/2.0, oy+1.2); break;   // a clear dot, not a droppable speck
        default: break;
        }
    };

    tp << "; PA Line test (garethky / K-factor) — generated toolpath\n";
    tp << "G90\nM83\nG92 E0.0\n";
    tp << ";\n; anchor frame (peel handle)\n;\n";
    auto bar = [&](double x, double dir) {
        travel(x, bar_yB);
        unretract();
        seg(x, bar_yT, bar_yT - bar_yB, slowF);
        tp << "G1 X" << fc(x + dir * bead) << " Y" << fc(bar_yT) << " F" << travF << " ; shift\n";
        seg(x + dir * bead, bar_yB, bar_yT - bar_yB, slowF);
        do_retract();
    };
    bar(xL, +1.0);
    bar(xR, -1.0);

    tp << ";\n; test lines (front = start PA -> back = end PA)\n;\n";
    for (int i = 0; i < num_lines; ++i) {
        const double pa = start_pa + i * step;
        const double y  = y0 + i * spacing;
        travel(xL, y);
        tp << pa_cmd(pa) << " ; set Pressure Advance\n";
        unretract();
        accel(6000, 6000);   // test acceleration
        seg(xB1, y, slow_len, slowF);
        seg(xB2, y, fast_len, fastF);
        seg(xR,  y, end_len,  slowF);
        accel(500, 500);
        do_retract();
    }

    tp << ";\n; reference ticks at the slow/fast boundaries\n;\n";
    for (double xt : { xB1, xB2 }) {
        travel(xt, tick_yB);
        unretract();
        seg(xt, tick_yT, tick_len, slowF);
        do_retract();
    }

    // --- PA value labels, right of the right bar (~every 0.01, plus the last) ---
    tp << ";\n; PA value labels\n;\n";
    {
        // Digits are 7-segment strokes of single ~nozzle-width beads, so they need
        // to be large enough that the segments and the gaps between them resolve at
        // this nozzle size (3 x 6 mm reads cleanly on a 0.4–0.6 nozzle).
        const double cw = 3.0, cht = 6.0, csp = cw + 1.0, dotw = cw * 0.45 + 0.5;
        // Label about every 0.01, but never closer than the glyph height + a gap,
        // so the taller digits don't collide with the next label.
        const int    label_every = std::max({ 1, (int) std::lround(0.01 / step),
                                              (int) std::ceil((cht + 2.0) / spacing) });
        const double lx0 = xR + 3.0;
        // One Z-hop carries the nozzle over the pattern into the clear label column.
        tp << "G1 Z" << fz(zhi) << " F720 ; lift\n";
        tp << "G1 X" << fc(lx0) << " Y" << fc(y0) << " F" << travF << " ; to labels\n";
        tp << "G1 Z" << fz(lh + z_offset) << " F720 ; lower\n";
        for (int i = 0; i < num_lines; ++i) {
            if (i % label_every != 0 && i != num_lines - 1)
                continue;
            const double pa = start_pa + i * step;
            const double oy = (y0 + i * spacing) - cht / 2.0;
            double lx = lx0;
            for (char ch : fmt3(pa)) {
                glyph(ch, lx, oy, cw, cht);
                lx += (ch == '.') ? dotw : csp;
            }
        }
    }
    tp << pa_cmd(0.0) << " ; reset Pressure Advance\n";

    // --- Write the toolpath body to a unique temp file ---
    // Unique per run so a stale body from an earlier run (or another PrusaSlicer
    // instance) can never be spliced in by mistake. The body is a transient artifact:
    // if the project is saved and reopened, or temp is cleaned before export, the
    // splicer fails loudly (see CalibrationPALinePostProcessor) rather than splicing a
    // wrong/stale body.
    const boost::filesystem::path body_path =
        boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("pa_line_body-%%%%-%%%%-%%%%.gcode");
    {
        boost::nowide::ofstream bf(body_path.string(), std::ios::binary | std::ios::trunc);
        if (!bf.is_open()) {
            wxMessageBox(_L("Failed to write PA line toolpath."), _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }
        bf << tp.str();
    }

    // --- Placeholder object (its sliced body is replaced by the toolpath) ---
    const boost::filesystem::path stl_path =
        boost::filesystem::temp_directory_path() / "pa_line_placeholder.stl";
    {
        indexed_triangle_set ph = its_make_cube(8.0, 8.0, lh);
        its_translate(ph, Vec3f(float(bed_c.x() - 4.0), float(bed_c.y() - 4.0), 0.0f));
        if (!its_write_stl_binary(stl_path.string().c_str(), "pa_line", ph)) {
            wxMessageBox(_L("Failed to write PA line placeholder."), _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }
    }
    // The splicer replaces the body of the FIRST "; printing object" boundary after
    // the marker, so the placeholder must be the only object on the plate. The menu
    // clears the plate before opening this dialog; reset here too so the splice can
    // never target a stray object (or a leftover placeholder) instead of ours.
    plater->reset();
    std::vector<size_t> loaded = plater->load_files({ stl_path }, true, false);
    boost::filesystem::remove(stl_path);
    if (loaded.empty()) return false;

    // --- Marker: the SOLE first-layer custom gcode (gates the post-processor) ---
    {
        Model& model = wxGetApp().model();
        CustomGCode::Info& cg = model.custom_gcode_per_print_z();
        cg.mode = CustomGCode::SingleExtruder;
        cg.gcodes.clear();
        CustomGCode::Item marker;
        marker.print_z  = lh / 2.0;
        marker.type     = CustomGCode::Custom;
        marker.extruder = 1;
        marker.color    = "";
        marker.extra    = std::string("; ") + calibration_pa_marker() + "\n";
        cg.gcodes.push_back(marker);
    }

    // --- Config: single layer, OctoPrint-labelled body, ASCII, no skirt/brim/support ---
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& c = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        c.set_key_value("layer_height", new ConfigOptionFloat(lh));
        c.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(lh, false));
        c.set_key_value("complete_objects", new ConfigOptionBool(false));
        c.set_key_value("skirts", new ConfigOptionInt(0));
        // The placeholder isn't printed (its body is spliced out), and the generated
        // toolpath has its own anchor bars, so the brim checkbox does not apply here.
        c.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        c.set_key_value("support_material", new ConfigOptionBool(false));
        // The body hard-codes its Z moves to the layer height, so a raft (driven by
        // raft_layers, not support_material) would lift the object and drop the
        // generated toolpath back down into the raft. Force rafts off too.
        c.set_key_value("raft_layers", new ConfigOptionInt(0));
        c.set_key_value("gcode_label_objects",
            new ConfigOptionEnum<LabelObjectsStyle>(LabelObjectsStyle::Octoprint));
        // Pin every role to the tool the body was generated for. Two reasons:
        //   * the discard above would otherwise drop an unsaved perimeter_extruder edit,
        //     so the body would be built for one tool and the export sliced with another;
        //   * if the roles resolved to DIFFERENT tools, set_extruder() emits
        //     maybe_stop_instance() before each toolchange (GCode.cpp:3966), splitting the
        //     placeholder across several "; printing object"/"; stop printing object"
        //     pairs. The splicer replaces the first pair only, so the rest of the
        //     placeholder would survive into the print as a stray blob.
        const int pin = int(extruder_idx) + 1;   // config is 1-based
        c.set_key_value("perimeter_extruder",    new ConfigOptionInt(pin));
        c.set_key_value("infill_extruder",       new ConfigOptionInt(pin));
        c.set_key_value("solid_infill_extruder", new ConfigOptionInt(pin));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }
    {
        DynamicPrintConfig& pr = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        pr.set_key_value("binary_gcode", new ConfigOptionBool(false));
        // The generated body uses relative, LINEAR E (M83 + filament-length per mm).
        // Force the whole export to match — as the Flow Rate and Retraction
        // calibrations do — so the slicer's preamble/tail agrees with the spliced body:
        //   * relative E, else an M82 (absolute) preset misreads the kept end-G-code
        //     retract/shutdown as relative and blobs;
        //   * linear E, else a volumetric preset reads the body's E words as mm^3 and
        //     under-extrudes the sweep.
        pr.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));
        pr.set_key_value("use_volumetric_e", new ConfigOptionBool(false));
        // The body balances retracts with literal "G1 E" moves, so firmware retraction
        // must be off: otherwise a slicer-emitted firmware retract (G10) before the
        // spliced body is never matched by a G11, leaving the retract state active and
        // mis-priming the test.
        pr.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));
        // The kept end G-code's wipe-on-retract would run along the placeholder's
        // stored wipe path at bed center, dragging the nozzle across the finished
        // pattern. Force wipe off.
        // Sized to the printer's extruder count: a 1-element vector on a multi-extruder
        // preset is tolerated at slice time but persists at length 1 if the user saves.
        pr.set_key_value("wipe", new ConfigOptionBools(n_extruders, false));
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();
    }

    // --- Wire the body-splicing post-processor ---
    {
        DynamicPrintConfig& c = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        std::vector<std::string> scripts;
        if (const auto* pp = c.option<ConfigOptionStrings>("post_process")) scripts = pp->values;
        scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
                          [](const std::string& s) {
                              // Drop a stale Line URL, or a leftover Pattern URL from a
                              // pre-1.9.2 preset (the Pattern test was removed).
                              return is_pa_line_url(s) ||
                                     s.rfind("::builtin::pa_calibration", 0) == 0;
                          }),
                      scripts.end());
        scripts.insert(scripts.begin(), make_pa_line_url(body_path.string()));
        c.set_key_value("post_process", new ConfigOptionStrings(scripts));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }
    plater->changed_objects(loaded);
    plater->set_pa_line_export_reminder(true);   // remind, after slicing, that the preview is a placeholder
    BOOST_LOG_TRIVIAL(info) << "PA line calibration: " << num_lines << " lines, toolpath " << body_path.string();

    if (auto* nm = wxGetApp().notification_manager()) {
        nm->push_notification(NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::WarningNotificationLevel,
            "PA line (K-factor) test: a generated toolpath replaces the placeholder when you "
            "slice. Lines run front = start PA -> back = end PA, welded to side anchor bars "
            "(peel the whole test off by a bar). The on-screen preview shows the placeholder; "
            "EXPORT the G-code to see the real pattern. These overrides are temporary - revert "
            "via the revert buttons on the Print AND Printer tabs before slicing other models.");
    }
    apply_calibration_filename_prefix("PressureAdvance");
    return true;
}

}} // namespace Slic3r::GUI
