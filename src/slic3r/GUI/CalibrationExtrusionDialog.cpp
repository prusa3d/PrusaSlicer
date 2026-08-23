///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationExtrusionDialog.hpp"
#include "CalibrationCommon.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// Cube dimensions for the extrusion multiplier test (mm)
static constexpr double CUBE_SIZE = 40.0;

CalibrationExtrusionDialog::CalibrationExtrusionDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Extrusion Multiplier Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Description
    auto* desc = new wxStaticText(this, wxID_ANY,
        _L("This test generates a 40×40×40 mm cube printed in vase mode\n"
           "with a single classic perimeter and no bottom layers.\n\n"
           "After printing, measure the wall thickness with calipers\n"
           "and adjust the extrusion multiplier accordingly:\n\n"
           "  new_multiplier = expected_width / measured_width\n\n"
           "Optionally add a brim for bed adhesion."));
    sizer->Add(desc, 0, wxALL, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

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

bool CalibrationExtrusionDialog::generate_and_load()
{
    BOOST_LOG_TRIVIAL(info) << "Generating extrusion multiplier calibration cube: "
                            << CUBE_SIZE << "x" << CUBE_SIZE << "x" << CUBE_SIZE << " mm";

    // Generate a simple cube centred at XY origin
    auto its = its_make_cube(CUBE_SIZE, CUBE_SIZE, CUBE_SIZE);
    its_translate(its, Vec3f(float(-CUBE_SIZE / 2.0), float(-CUBE_SIZE / 2.0), 0.f));

    // Write to temp STL for loading via plater
    boost::filesystem::path stl_path = boost::filesystem::temp_directory_path() / "extrusion_cube.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "extrusion_cube", its)) {
        wxMessageBox(_L("Failed to write extrusion multiplier cube STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load the STL onto the bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Reset print config to saved preset before applying overrides
    wxGetApp().preset_bundle->prints.discard_current_changes();

    // Configure for extrusion multiplier test:
    // - Spiral vase mode (single wall, continuous)
    // - Classic perimeter generator (constant width, easier to measure)
    // - No bottom/top solid layers
    // - No infill
    // - Variable layer height disabled
    // - 5 mm brim for adhesion
    {
        DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("spiral_vase", new ConfigOptionBool(true));
        config.set_key_value("bottom_solid_layers", new ConfigOptionInt(0));
        config.set_key_value("top_solid_layers", new ConfigOptionInt(0));
        config.set_key_value("perimeters", new ConfigOptionInt(1));
        config.set_key_value("fill_density", new ConfigOptionPercent(0));
        config.set_key_value("perimeter_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Classic));
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    apply_calibration_filename_prefix("ExtrusionMultiplier");

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
