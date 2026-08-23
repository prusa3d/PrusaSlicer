///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationFlowRateDialog.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/GCode/CalibrationFlowPostProcessor.hpp"
#include "CalibrationCommon.hpp"

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

CalibrationFlowRateDialog::CalibrationFlowRateDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Flow Rate Calibration (YOLO)"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* desc = new wxStaticText(this, wxID_ANY,
        _L("YOLO Flow Calibration — prints flat pads with Archimedean Chords\n"
           "spiral pattern at varying extrusion multipliers.\n"
           "Pick the pad with the smoothest top surface — no gaps between\n"
           "arcs and no material buildup at the inner spiral."));
    sizer->Add(desc, 0, wxALL, 15);

    auto* grid = new wxFlexGridSizer(4, 2, 10, 15);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Steps each side of center:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_num_steps = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, 5);
    grid->Add(m_num_steps, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Step size (%):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_step_pct = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, 1);
    grid->Add(m_step_pct, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Pad width (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pad_width = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 20, 80, 30);
    grid->Add(m_pad_width, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Pad depth (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pad_depth = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 20, 80, 20);
    grid->Add(m_pad_depth, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_num_steps);
    wxGetApp().UpdateDarkUI(m_step_pct);
    wxGetApp().UpdateDarkUI(m_pad_width);
    wxGetApp().UpdateDarkUI(m_pad_depth);
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

bool CalibrationFlowRateDialog::generate_and_load()
{
    int num_steps  = m_num_steps->GetValue();
    int step_pct   = m_step_pct->GetValue();
    int pad_w      = m_pad_width->GetValue();
    int pad_d      = m_pad_depth->GetValue();
    int total_pads = 2 * num_steps + 1;

    double nozzle_diameter = 0.4;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* nd_opt = pb->printers.get_selected_preset()
                                 .config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nd_opt && !nd_opt->empty())
            nozzle_diameter = nd_opt->get_at(0);
    }

    double layer_height = nozzle_diameter / 2.0;
    double pad_h = layer_height + 6.0 * layer_height;  // 7 layers

    BOOST_LOG_TRIVIAL(info) << "YOLO Flow calibration: nozzle=" << nozzle_diameter
                            << " layer_h=" << layer_height
                            << " pad_h=" << pad_h
                            << " pads=" << total_pads;

    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    // --- Print settings ---
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;

        config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        // Print the pads interleaved (all at the same Z), never sequentially:
        // they must reach the same height for a fair comparison, and sequential
        // printing (complete_objects) suppresses custom_gcode_per_print_z, so
        // the calibration marker the post-processor keys on would never emit.
        config.set_key_value("complete_objects", new ConfigOptionBool(false));

        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));

        config.set_key_value("perimeters", new ConfigOptionInt(1));
        // 6 solid monotonic layers + 1 spiral top layer
        config.set_key_value("top_solid_layers", new ConfigOptionInt(1));
        config.set_key_value("bottom_solid_layers", new ConfigOptionInt(6));
        config.set_key_value("fill_density", new ConfigOptionPercent(100));
        config.set_key_value("bottom_fill_pattern",
            new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        config.set_key_value("top_fill_pattern",
            new ConfigOptionEnum<InfillPattern>(ipArchimedeanChords));
        config.set_key_value("ironing", new ConfigOptionBool(false));

        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
        wxGetApp().plater()->on_config_change(wxGetApp().preset_bundle->full_config());
    }

    // Clean, self-describing export filename ("FlowRate_..." instead of "-_...";
    // see apply_calibration_filename_prefix for why the default collapses to "-").
    apply_calibration_filename_prefix("FlowRate");

    // --- Generate geometry ---
    boost::filesystem::path tmp_dir = boost::filesystem::temp_directory_path();
    std::vector<boost::filesystem::path> stl_paths;

    double corner_r = 1.5;
    double tab_w    = 17.0;
    double tab_d    = 8.0;
    double tab_h    = layer_height;  // single layer thick

    for (int i = 0; i < total_pads; ++i) {
        int flow_offset_pct = -num_steps * step_pct + i * step_pct;

        // Rectangular rounded pad
        auto pad = Slic3r::make_rounded_rect_pad(double(pad_w), double(pad_d), pad_h, corner_r);

        // Thin label tab at the back
        auto tab = Slic3r::make_rounded_rect_pad(tab_w, tab_d, tab_h, corner_r);
        double tab_x_offset = (double(pad_w) - tab_w) / 2.0;
        its_translate(tab, Vec3f(float(tab_x_offset), float(pad_d), 0.f));
        its_merge(pad, tab);

        // Raised text on the tab
        {
            char lbuf[16];
            std::string text_label;
            if (flow_offset_pct == 0)
                text_label = "0";
            else if (flow_offset_pct > 0) {
                snprintf(lbuf, sizeof(lbuf), ".%02d", flow_offset_pct);
                text_label = lbuf;
            } else {
                snprintf(lbuf, sizeof(lbuf), "-.%02d", -flow_offset_pct);
                text_label = lbuf;
            }

            double text_size = std::min(5.0, tab_d * 0.6);
            auto text_mesh = Slic3r::make_block_text(text_label, text_size, 0.4, false);
            if (!text_mesh.empty()) {
                for (auto& v : text_mesh.vertices)
                    std::swap(v.y(), v.z());
                for (auto& f : text_mesh.indices)
                    std::swap(f[0], f[1]);
                its_translate(text_mesh, Vec3f(
                    float(double(pad_w) / 2.0),
                    float(pad_d + tab_d / 2.0),
                    float(tab_h)));
                its_merge(pad, text_mesh);
            }
        }

        // Safe filename
        char label_buf[16];
        if (flow_offset_pct == 0)
            snprintf(label_buf, sizeof(label_buf), "0");
        else if (flow_offset_pct > 0)
            snprintf(label_buf, sizeof(label_buf), "p%02d", flow_offset_pct);
        else
            snprintf(label_buf, sizeof(label_buf), "m%02d", -flow_offset_pct);

        std::string filename = std::string("flow_") + label_buf + ".stl";
        boost::filesystem::path stl_path = tmp_dir / filename;

        if (!its_write_stl_binary(stl_path.string().c_str(), filename.c_str(), pad)) {
            wxMessageBox(_L("Failed to write flow rate pad STL."),
                         _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }
        stl_paths.push_back(stl_path);
    }

    // --- Load and configure objects ---
    std::vector<size_t> loaded_object_idxs = plater->load_files(stl_paths, true, false);

    Model& model = wxGetApp().model();
    if (loaded_object_idxs.size() < (size_t)total_pads) {
        BOOST_LOG_TRIVIAL(error) << "YOLO Flow calibration: expected " << total_pads
                                 << " objects but only " << loaded_object_idxs.size() << " loaded";
        return false;
    }

    // (object name -> E scale factor) handed to the post-processor below.
    std::vector<std::pair<std::string, double>> object_factors;
    object_factors.reserve(total_pads);

    for (int i = 0; i < total_pads && i < (int)loaded_object_idxs.size(); ++i) {
        int flow_offset_pct = -num_steps * step_pct + i * step_pct;
        double multiplier = 1.0 + flow_offset_pct / 100.0;

        size_t obj_idx = loaded_object_idxs[i];
        if (obj_idx >= model.objects.size() || model.objects[obj_idx] == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "YOLO Flow calibration: invalid object index " << obj_idx;
            return false;
        }
        ModelObject* obj = model.objects[obj_idx];

        // Object name = flow modifier label. This same string identifies the
        // object in the M486 / EXCLUDE_OBJECT markers the slicer emits, so the
        // post-processor keys its per-pad scale factor on it.
        char name_buf[32];
        if (flow_offset_pct == 0)
            snprintf(name_buf, sizeof(name_buf), "0");
        else if (flow_offset_pct > 0)
            snprintf(name_buf, sizeof(name_buf), ".%02d", flow_offset_pct);
        else
            snprintf(name_buf, sizeof(name_buf), "-.%02d", -flow_offset_pct);
        obj->name = name_buf;

        // NOTE: extrusion_multiplier is a GCodeConfig (per-extruder) option, not
        // a PrintObjectConfig/PrintRegionConfig one, so setting it on the object
        // config is silently dropped at slice time and every pad would print at
        // the same flow (issue #20). The per-pad flow is instead applied by the
        // in-process post-processor wired up below, which scales each pad's
        // deposition E by `multiplier` relative to the filament's current flow.
        object_factors.emplace_back(obj->name, multiplier);

        // Enable special Archimedean Chords ordering (ported from OrcaSlicer)
        obj->config.set_key_value("calib_flowrate_topinfill_special_order",
            new ConfigOptionBool(true));
        BOOST_LOG_TRIVIAL(info) << "Flow pad " << obj->name
                                << ": E scale factor=" << multiplier;
    }

    // --- Inject the job-scoped calibration marker ---
    //
    // The post_process hook below lives in the print *preset* and runs for every
    // later slice; the post-processor only rewrites a G-code that carries this
    // marker, so a normal print is passed through untouched. custom_gcode_per_print_z
    // belongs to the model, so loading any other model clears the marker.
    {
        CustomGCode::Info& cg = model.custom_gcode_per_print_z();
        const std::string marker_extra = std::string("; ") + calibration_flow_marker() + "\n";
        // Drop only a stale marker from a previous calibration run (idempotent);
        // preserve any unrelated custom per-Z entries the user already has on the
        // bed (pauses, color changes, ...) rather than clearing the whole list.
        cg.gcodes.erase(std::remove_if(cg.gcodes.begin(), cg.gcodes.end(),
                            [&](const CustomGCode::Item& it) { return it.extra == marker_extra; }),
                        cg.gcodes.end());
        if (cg.gcodes.empty())
            cg.mode = CustomGCode::SingleExtruder;
        CustomGCode::Item marker;
        marker.print_z  = layer_height / 2.0;   // first layer
        marker.type     = CustomGCode::Custom;
        marker.extruder = 1;
        marker.color    = "";
        marker.extra    = marker_extra;
        cg.gcodes.push_back(marker);
        std::sort(cg.gcodes.begin(), cg.gcodes.end());
    }

    // --- Pin the printer output options the post-processor relies on ---
    wxGetApp().preset_bundle->printers.discard_current_changes();
    {
        DynamicPrintConfig& printer_config =
            wxGetApp().preset_bundle->printers.get_edited_preset().config;
        // Relative, linear E: the post-processor scales each move's E delta, which
        // is only valid for relative (M83) non-volumetric extrusion.
        printer_config.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));
        printer_config.set_key_value("use_volumetric_e", new ConfigOptionBool(false));
        // ASCII output. PrusaSlicer writes binary G-code before running
        // post-process scripts, so a .bgcode would be sealed and unrewritable.
        printer_config.set_key_value("binary_gcode", new ConfigOptionBool(false));
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();
    }

    // --- Wire up the in-process post-processor ---
    //
    // A `::builtin::` URL that PostProcessor.cpp intercepts and dispatches to
    // Slic3r::run_calibration_flow_post_processor() — no external interpreter.
    std::string builtin_url = make_calibration_flow_url(object_factors);
    {
        DynamicPrintConfig& print_config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        // Emit object boundaries so the post-processor can tell the pads apart.
        // gcode_label_objects is a PRINT option. Use OctoPrint style ("; printing
        // object <name>" comments): unlike Firmware (M486/EXCLUDE_OBJECT) it is
        // emitted for EVERY gcode flavor and carries the raw, unsanitized name,
        // so the calibration works on Marlin, RRF, Klipper and any other flavor.
        print_config.set_key_value("gcode_label_objects",
            new ConfigOptionEnum<LabelObjectsStyle>(LabelObjectsStyle::Octoprint));
        // Merge into any user post-processing scripts rather than replacing the
        // list; strip a stale flow hook from a previous run (idempotent), then
        // insert ours at the FRONT so it runs before any user script that
        // consumes/transforms the G-code.
        std::vector<std::string> scripts;
        if (const auto* pp = print_config.option<ConfigOptionStrings>("post_process"))
            scripts = pp->values;
        scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
                          [](const std::string& s) { return is_calibration_flow_url(s); }),
                      scripts.end());
        scripts.insert(scripts.begin(), builtin_url);
        print_config.set_key_value("post_process", new ConfigOptionStrings(scripts));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    BOOST_LOG_TRIVIAL(info) << "YOLO Flow calibration: post-process URL " << builtin_url;

    // --- Lay the pads out in a grid, ordered by flow offset ---
    //
    // load order == ascending flow offset (most negative .. most positive), so
    // a row-major fill (cols per row, left to right, back to front) reads
    // naturally, e.g. for the ±5%/1% default at 4 columns:
    //     -.05  -.04  -.03  -.02
    //     -.01    0    .01   .02
    //      .03   .04   .05
    // This replaces the auto-nester (plater->arrange), which packed the pads in
    // an arbitrary order so the labels read randomly on the bed. The column
    // count prefers 4 (the layout above) but is reduced to fit the bed width and
    // increased to fit the bed depth, so larger step counts / pad sizes still
    // land on small beds (e.g. a 180 mm MINI) instead of off the plate.
    //
    // The grid only applies to rectangular beds. For circular (delta) or custom
    // beds the bounding rectangle includes unprintable corners, so a grid that
    // "fits" could still drop pads off the plate; and if the pads don't fit even
    // an adapted rectangular grid, packing them is better than placing them off
    // the bed. In both cases fall back to the shape-aware arranger.
    bool placed_in_grid = false;
    if (plater->build_volume().type() == BuildVolume::Type::Rectangle) {
        // Footprint of one pad (all identical), including the back label tab.
        const BoundingBoxf3 fp = model.objects[loaded_object_idxs[0]]->instance_bounding_box(0);
        const double cell_w = (fp.max.x() - fp.min.x()) + 5.0;  // + inter-pad gap
        const double cell_h = (fp.max.y() - fp.min.y()) + 5.0;

        const BoundingBoxf bed2d   = plater->build_volume().bounding_volume2d();
        const Vec2d        bed_c   = bed2d.center();
        const double       usable_w = bed2d.size().x() - 10.0;  // keep off the edges
        const double       usable_h = bed2d.size().y() - 10.0;

        const int max_cols = std::max(1, int(std::floor(usable_w / cell_w)));
        int       cols     = std::min(4, max_cols);            // prefer 4 columns
        // Add columns (fewer rows) until the grid is short enough for the bed.
        while (cols < max_cols &&
               double((total_pads + cols - 1) / cols) * cell_h > usable_h)
            ++cols;
        const int rows = (total_pads + cols - 1) / cols;

        if (rows * cell_h <= usable_h && cols * cell_w <= usable_w) {
            const double grid_left = bed_c.x() - cols * cell_w / 2.0;
            const double grid_top  = bed_c.y() + rows * cell_h / 2.0;

            for (int i = 0; i < total_pads && i < (int)loaded_object_idxs.size(); ++i) {
                ModelObject* obj = model.objects[loaded_object_idxs[i]];
                if (obj->instances.empty())
                    continue;
                const int col = i % cols;
                const int row = i / cols;
                // Cell center; row 0 sits at the back (max Y) so the grid reads
                // top-to-bottom the way the labels are listed above.
                const Vec2d target(grid_left + cell_w * (col + 0.5),
                                   grid_top  - cell_h * (row + 0.5));
                const BoundingBoxf3 bb = obj->instance_bounding_box(0);
                const Vec2d cur(0.5 * (bb.min.x() + bb.max.x()),
                                0.5 * (bb.min.y() + bb.max.y()));
                const Vec2d delta = target - cur;
                ModelInstance* inst = obj->instances.front();
                inst->set_offset(inst->get_offset() + Vec3d(delta.x(), delta.y(), 0.0));
                obj->invalidate_bounding_box();
            }
            placed_in_grid = true;
        }
    }
    if (!placed_in_grid) {
        BOOST_LOG_TRIVIAL(info)
            << "YOLO Flow calibration: pads do not fit a flow-ordered grid on this bed "
               "(non-rectangular or too small); using the arranger instead.";
        plater->arrange(true);
    }

    // load_files() schedules a slice using the just-imported defaults, so the
    // per-object overrides need an explicit invalidation pass. This also
    // reloads the 3D scene so the grid placement above is reflected.
    plater->changed_objects(loaded_object_idxs);

    // Clean up temp files
    for (const auto& p : stl_paths)
        boost::filesystem::remove(p);

    return true;
}

}} // namespace Slic3r::GUI
