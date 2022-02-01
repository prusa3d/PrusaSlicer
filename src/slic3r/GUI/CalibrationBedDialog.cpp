#include "CalibrationBedDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "GLCanvas3D.hpp"
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

void CalibrationBedDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxButton* bt = new wxButton(this, wxID_FILE1, _L("Generate"));
    bt->Bind(wxEVT_BUTTON, &CalibrationBedDialog::create_geometry, this);
    buttons->Add(bt);
}

void CalibrationBedDialog::create_geometry(wxCommandEvent& event_args) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    if (!plat->new_project(L("First layer calibration")))
        return;

    GLCanvas3D::set_warning_freeze(true);
    bool autocenter = gui_app->app_config->get("autocenter") == "1";
    if(autocenter) {
        //disable aut-ocenter for this calibration.
        gui_app->app_config->set("autocenter", "0");
    }
    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "bed_leveling" / "patch.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "bed_leveling" / "patch.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "bed_leveling" / "patch.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "bed_leveling" / "patch.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "bed_leveling" / "patch.amf").string()}, true, false, false);

    assert(objs_idx.size() == 5);
    const DynamicPrintConfig* printConfig = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();
    const DynamicPrintConfig* printerConfig = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();
    
    /// --- scale ---
    //model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter = printerConfig->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter->values.size() > 0);
    float xyScale = nozzle_diameter->values[0] / 0.4;
    //scale z with the first_layer_height
    const ConfigOptionFloatOrPercent* first_layer_height = printConfig->option<ConfigOptionFloatOrPercent>("first_layer_height");
    float zscale = first_layer_height->get_abs_value(nozzle_diameter->values[0]) / 0.2;
    //do scaling
    if (xyScale < 0.9 || 1.2 < xyScale) {
        for (size_t i = 0; i < 5; i++)
            model.objects[objs_idx[i]]->scale(xyScale, xyScale, zscale);
    } else {
        for (size_t i = 0; i < 5; i++)
            model.objects[objs_idx[i]]->scale(1, 1, zscale);
    }

    /// --- rotate ---
    const ConfigOptionPoints* bed_shape = printerConfig->option<ConfigOptionPoints>("bed_shape");
    if (bed_shape->values.size() == 4) {
        model.objects[objs_idx[0]]->rotate(PI / 4, { 0,0,1 });
        model.objects[objs_idx[1]]->rotate(5 * PI / 4, { 0,0,1 });
        model.objects[objs_idx[3]]->rotate(3 * PI / 4, { 0,0,1 });
        model.objects[objs_idx[4]]->rotate(7 * PI / 4, { 0,0,1 });
    } else {
        model.objects[objs_idx[3]]->rotate(PI / 2, { 0,0,1 });
        model.objects[objs_idx[4]]->rotate(PI / 2, { 0,0,1 });
    }

    /// --- translate ---
    //three first will stay with this orientation (top left, middle, bottom right)
    //last two with 90deg (top left, middle, bottom right)
    //get position for patches
    Vec2d bed_size = BoundingBoxf(bed_shape->values).size();
    Vec2d bed_min = BoundingBoxf(bed_shape->values).min;
    float offsetx = 10 + 10 * xyScale;
    float offsety = 10 + 10 * xyScale;
    if (bed_shape->values.size() > 4) {
        offsetx = bed_size.x() / 2 - bed_size.x() * 1.414 / 4 + 10 * xyScale;
        offsety = bed_size.y() / 2 - bed_size.y() * 1.414 / 4 + 10 * xyScale;
    }
    bool large_enough = bed_shape->values.size() == 4 ?
        (bed_size.x() > offsetx * 3 && bed_size.y() > offsety * 3) :
        (bed_size.x() > offsetx * 2 + 10 * xyScale && bed_size.y() > offsety * 2 + 10 * xyScale);
    if (!large_enough){
        //problem : too small, use arrange instead and let the user place them.
        plat->arrange();
        //TODO add message
    } else {
        model.objects[objs_idx[0]]->translate({ bed_min.x() + offsetx,               bed_min.y() + bed_size.y() - offsety,0 });
        model.objects[objs_idx[1]]->translate({ bed_min.x() + bed_size.x() - offsetx,bed_min.y() + offsety ,              0 });
        model.objects[objs_idx[2]]->translate({ bed_min.x() + bed_size.x()/2,       bed_min.y() + bed_size.y() / 2,     0 });
        model.objects[objs_idx[3]]->translate({ bed_min.x() + offsetx,               bed_min.y() + offsety,               0 });
        model.objects[objs_idx[4]]->translate({ bed_min.x() + bed_size.x() - offsetx,bed_min.y() + bed_size.y() - offsety,0 });
    }

    /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *printConfig; //make a copy
    new_print_config.set_key_value("complete_objects", new ConfigOptionBool(true));

    /// --- custom config ---
    for (size_t i = 0; i < 5; i++) {
        model.objects[objs_idx[i]]->config.set_key_value("perimeters", new ConfigOptionInt(2));
        model.objects[objs_idx[i]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(2));
        model.objects[objs_idx[i]]->config.set_key_value("gap_fill_enabled", new ConfigOptionBool(false));
        model.objects[objs_idx[i]]->config.set_key_value("first_layer_extrusion_width", new ConfigOptionFloatOrPercent(140, true));
        model.objects[objs_idx[i]]->config.set_key_value("bottom_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        //disable ironing post-process
        model.objects[objs_idx[i]]->config.set_key_value("ironing", new ConfigOptionBool(false));
    }
    if (bed_shape->values.size() == 4) {
        model.objects[objs_idx[0]]->config.set_key_value("fill_angle", new ConfigOptionFloat(90));
        model.objects[objs_idx[1]]->config.set_key_value("fill_angle", new ConfigOptionFloat(90));
        model.objects[objs_idx[2]]->config.set_key_value("fill_angle", new ConfigOptionFloat(45));
        model.objects[objs_idx[3]]->config.set_key_value("fill_angle", new ConfigOptionFloat(0));
        model.objects[objs_idx[4]]->config.set_key_value("fill_angle", new ConfigOptionFloat(0));
    } else {
        for (size_t i = 0; i < 3; i++)
        for (size_t i = 3; i < 5; i++)
            model.objects[objs_idx[i]]->config.set_key_value("fill_angle", new ConfigOptionFloat(135));
    }

    //update plater
    GLCanvas3D::set_warning_freeze(false);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    plat->changed_objects(objs_idx);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->update_dirty();
    //update everything, easier to code.
    this->gui_app->obj_list()->update_after_undo_redo();
    //if(!plat->is_background_process_update_scheduled())
    //    plat->schedule_background_process();
    plat->reslice();

    if (autocenter) {
        //re-enable auto-center after this calibration.
        gui_app->app_config->set("autocenter", "1");
    }
}

} // namespace GUI
} // namespace Slic3r
