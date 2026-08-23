///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationShrinkageDialog.hpp"
#include "CalibrationCommon.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CalibrationModels.hpp"
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

CalibrationShrinkageDialog::CalibrationShrinkageDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Dimensional Accuracy / Shrinkage Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Description
    auto* desc = new wxStaticText(this, wxID_ANY,
        _L("Generates an XYZ cross gauge for dimensional accuracy measurement.\n"
           "After printing, measure each arm with calipers and compare\n"
           "to the target length. Grooves at 25mm intervals provide\n"
           "reference points for precise measurement."));
    sizer->Add(desc, 0, wxALL, 15);

    auto* grid = new wxFlexGridSizer(1, 2, 10, 15);

    // Target length
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Arm length (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_length = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 20, 200, 100);
    grid->Add(m_length, 0, wxEXPAND);

    sizer->Add(grid, 0, wxLEFT | wxRIGHT | wxEXPAND, 15);

    // Brim checkbox
    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_length);
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

bool CalibrationShrinkageDialog::generate_and_load()
{
    int length = m_length->GetValue();
    if (length < 20) {
        wxMessageBox(_L("Arm length must be at least 20mm."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    BOOST_LOG_TRIVIAL(info) << "Generating shrinkage gauge: length=" << length;

    int skipped_holes = 0;
    auto its = Slic3r::make_shrinkage_gauge(double(length), &skipped_holes);

    if (its.vertices.empty() || its.indices.empty()) {
        wxMessageBox(_L("Failed to generate shrinkage gauge geometry."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // #40: a boolean cut can fail on some geometry; the gauge is still usable but
    // is missing those measurement marks. Warn rather than silently ship a wrong
    // part (this should not happen now that the mesh is re-welded between cuts).
    if (skipped_holes > 0) {
        wxMessageBox(wxString::Format(
                         _L("%d measurement hole(s) could not be generated and are "
                            "missing from the gauge. The part is still usable, but "
                            "those distance marks will not be present."),
                         skipped_holes),
                     _L("Shrinkage gauge"), wxOK | wxICON_WARNING, this);
    }

    // Write to temp STL
    boost::filesystem::path stl_path =
        boost::filesystem::temp_directory_path() / "shrinkage_gauge.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "shrinkage_gauge", its)) {
        wxMessageBox(_L("Failed to write shrinkage gauge STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load onto bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Reset print config and apply overrides
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

    // No custom G-code needed — just print and measure

    apply_calibration_filename_prefix("DimAccuracy");

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
