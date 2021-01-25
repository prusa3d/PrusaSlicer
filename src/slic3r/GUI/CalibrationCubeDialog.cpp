#include "CalibrationCubeDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include <wx/scrolwin.h>
#include <wx/display.h>
#include <wx/file.h>
#include "wxExtensions.hpp"

#if ENABLE_SCROLLABLE
static wxSize get_screen_size(wxWindow* window)
{
    const auto idx = wxDisplay::GetFromWindow(window);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);
    return display.GetClientArea().GetSize();
}
#endif // ENABLE_SCROLLABLE

namespace Slic3r {
namespace GUI {

void CalibrationCubeDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxString choices_scale[] = { "10mm", "20mm", "30mm", "40mm" };
    scale = new wxComboBox(this, wxID_ANY, wxString{ "20mm" }, wxDefaultPosition, wxDefaultSize, 4, choices_scale);
    scale->SetToolTip(_(L("You can choose the dimension of the cube. It's a simple scale, you can modify it in the right panel yourself if you prefer. It's just quicker to select it here.")));
    scale->SetSelection(1);
    wxString choices_goal[] = { "Dimensional accuracy (default)" , "infill/perimeter overlap"/*, "external perimeter overlap"*/};
    calibrate = new wxComboBox(this, wxID_ANY, wxString{ "Dimensional accuracy (default)" }, wxDefaultPosition, wxDefaultSize, 2, choices_goal);
    calibrate->SetToolTip(_(L("Select a goal, this will change settings to increase the effects to search.")));
    calibrate->SetSelection(0);

    buttons->Add(new wxStaticText(this, wxID_ANY, wxString{ "dimension:" }));
    buttons->Add(scale);
    buttons->AddSpacer(40);
    buttons->Add(new wxStaticText(this, wxID_ANY, wxString{ "goal:" }));
    buttons->Add(calibrate);
    buttons->AddSpacer(40);

    wxButton* bt = new wxButton(this, wxID_FILE1, _(L("Standard Cube")));
    bt->Bind(wxEVT_BUTTON, &CalibrationCubeDialog::create_geometry_standard, this);
    bt->SetToolTip(_(L("Standard cubic xyz cube, with a flat top. Better for infill/perimeter overlap calibration.")));
    buttons->Add(bt);
    buttons->AddSpacer(10);
    bt = new wxButton(this, wxID_FILE1, _(L("Voron Cube")));
    bt->Bind(wxEVT_BUTTON, &CalibrationCubeDialog::create_geometry_voron, this);
    bt->SetToolTip(_(L("Voron cubic cube with many features inside, with a bearing slot on top. Better to check dimensional accuracy.")));
    buttons->Add(bt);
}

void CalibrationCubeDialog::create_geometry(std::string calibration_path) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    plat->reset();
    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            Slic3r::resources_dir()+"/calibration/cube/"+ calibration_path}, true, false, false);

    assert(objs_idx.size() == 1);
    const DynamicPrintConfig* printConfig = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();
    const DynamicPrintConfig* filamentConfig = this->gui_app->get_tab(Preset::TYPE_FILAMENT)->get_config();
    const DynamicPrintConfig* printerConfig = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();
    
    /// --- scale ---
    //model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter_config = printerConfig->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float cube_size = 30;
    if (calibration_path == "xyzCalibration_cube.amf")
        cube_size = 20;
    int idx_scale = scale->GetSelection();
    float xyzScale = (10/cube_size) * (idx_scale+1);
    //do scaling
    model.objects[objs_idx[0]]->scale(xyzScale, xyzScale, xyzScale);


    /// --- translate ---
    const ConfigOptionPoints* bed_shape = printerConfig->option<ConfigOptionPoints>("bed_shape");
    Vec2d bed_size = BoundingBoxf(bed_shape->values).size();
    Vec2d bed_min = BoundingBoxf(bed_shape->values).min;
    model.objects[objs_idx[0]]->translate({ bed_min.x() + bed_size.x() / 2, bed_min.y() + bed_size.y() / 2, 0 });

    /// --- custom config ---
    int idx_goal = calibrate->GetSelection();
    if (idx_goal == 1) {
        model.objects[objs_idx[0]]->config.set_key_value("perimeters", new ConfigOptionInt(1));
        model.objects[objs_idx[0]]->config.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipCubic));
    } else if (idx_goal == 2) {
        model.objects[objs_idx[0]]->config.set_key_value("perimeters", new ConfigOptionInt(3));
        //add full solid layers
    }

    //update plater
    plat->changed_objects(objs_idx);
    plat->is_preview_shown();
    //update everything, easier to code.
    ObjectList* obj = this->gui_app->obj_list();
    obj->update_after_undo_redo();


    plat->reslice();
    plat->select_view_3D("Preview");
}

} // namespace GUI
} // namespace Slic3r
