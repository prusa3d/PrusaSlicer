#include "CalibrationFlowDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "Jobs/ArrangeJob.hpp"
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

void CalibrationFlowDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxButton* bt = new wxButton(this, wxID_FILE1, _L("Generate 10% intervals around current value"));
    bt->Bind(wxEVT_BUTTON, &CalibrationFlowDialog::create_geometry_10, this);
    buttons->Add(bt);
    bt = new wxButton(this, wxID_FILE2, _L("Generate 2% intervals below current value"));
    bt->Bind(wxEVT_BUTTON, &CalibrationFlowDialog::create_geometry_2_5, this);
    buttons->Add(bt);
}


void CalibrationFlowDialog::create_geometry(float start, float delta) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    if (!plat->new_project(L("Flow calibration")))
        return;

    GLCanvas3D::set_warning_freeze(true);
    bool autocenter = gui_app->app_config->get("autocenter") == "1";
    if (autocenter) {
        //disable auto-center for this calibration.
        gui_app->app_config->set("autocenter", "0");
    }

    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "filament_flow_test_cube.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "filament_flow_test_cube.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "filament_flow_test_cube.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "filament_flow_test_cube.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "filament_flow_test_cube.amf").string()}, true, false, false);


    assert(objs_idx.size() == 5);
    const DynamicPrintConfig* print_config = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();
    const DynamicPrintConfig* printerConfig = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();
    
    /// --- scale ---
    // model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter_config = printerConfig->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float xyScale = nozzle_diameter / 0.4;
    //scale z to have 6 layers
    const ConfigOptionFloatOrPercent* first_layer_height_setting = print_config->option<ConfigOptionFloatOrPercent>("first_layer_height");
    double first_layer_height = first_layer_height_setting->get_abs_value(nozzle_diameter);
    double layer_height = nozzle_diameter / 2.;
    first_layer_height = std::max(first_layer_height, nozzle_diameter / 2.);

    float zscale = first_layer_height + 5 * layer_height;
    //do scaling
    if (xyScale < 0.9 || 1.2 < xyScale) {
        for (size_t i = 0; i < 5; i++)
            model.objects[objs_idx[i]]->scale(xyScale, xyScale, zscale);
    } else {
        for (size_t i = 0; i < 5; i++)
            model.objects[objs_idx[i]]->scale(1, 1, zscale);
    }

    //add sub-part after scale
    float zscale_number = (first_layer_height + layer_height) / 0.4;
    /* zshift is calculated using the following:
    (zscale / 2) represents the midpoint of the filament_flow_test_cube
    ((first_layer_height + layer_height) / 2) represents the midpoint of our indicator tab (it is scaled to be 2 layers tall)
    The 0.3 constant is the same as the delta calculated in add_part below, this should probably be calculated per the model object
    */
    float zshift = -(zscale / 2) + ((first_layer_height + layer_height) / 2) + 0.3;
    if (delta == 10.f && start == 80.f) {
        add_part(model.objects[objs_idx[0]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "m20.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number});
        add_part(model.objects[objs_idx[1]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "m10.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number });
        add_part(model.objects[objs_idx[2]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "_0.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number });
        add_part(model.objects[objs_idx[3]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "p10.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number });
        add_part(model.objects[objs_idx[4]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "p20.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number });
    } else if (delta == 2.f && start == 92.f) {
        add_part(model.objects[objs_idx[0]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "m8.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number});
        add_part(model.objects[objs_idx[1]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "m6.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number});
        add_part(model.objects[objs_idx[2]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "m4.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number});
        add_part(model.objects[objs_idx[3]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "m2.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number});
        add_part(model.objects[objs_idx[4]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "_0.amf").string(), Vec3d{ 10 * xyScale,0,zshift }, Vec3d{ xyScale , xyScale, zscale_number});
    }
    for (size_t i = 0; i < 5; i++) {
        add_part(model.objects[objs_idx[i]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_flow" / "O.amf").string(), Vec3d{ 0,0,zscale/2.f + 0.5 }, Vec3d{xyScale , xyScale, layer_height / 0.2});
    }

    /// --- translate ---;
    bool has_to_arrange = false;
    const ConfigOptionFloat* extruder_clearance_radius = print_config->option<ConfigOptionFloat>("extruder_clearance_radius");
    const ConfigOptionPoints* bed_shape = printerConfig->option<ConfigOptionPoints>("bed_shape");
    const double brim_width = nozzle_diameter * 3.5;
    Vec2d bed_size = BoundingBoxf(bed_shape->values).size();
    Vec2d bed_min = BoundingBoxf(bed_shape->values).min;
    float offsetx = 3 + 20 * xyScale + extruder_clearance_radius->value + brim_width + (brim_width > extruder_clearance_radius->value ? brim_width - extruder_clearance_radius->value : 0);
    float offsety = 3 + 20 * xyScale + extruder_clearance_radius->value + brim_width + (brim_width > extruder_clearance_radius->value ? brim_width - extruder_clearance_radius->value : 0);
    model.objects[objs_idx[0]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx / 2, bed_min.y() + bed_size.y() / 2 - offsety, 0 });
    model.objects[objs_idx[1]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx / 2, bed_min.y() + bed_size.y() / 2          , 0 });
    model.objects[objs_idx[2]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx / 2, bed_min.y() + bed_size.y() / 2 + offsety, 0 });
    model.objects[objs_idx[3]]->translate({ bed_min.x() + bed_size.x() / 2 + offsetx / 2, bed_min.y() + bed_size.y() / 2 - offsety, 0 });
    model.objects[objs_idx[4]]->translate({ bed_min.x() + bed_size.x() / 2 + offsetx / 2, bed_min.y() + bed_size.y() / 2 + offsety, 0 });

    // if not enough space, forget about complete_objects
    if (bed_size.y() < offsety * 2 + 25 * xyScale + brim_width || bed_size.x() < offsetx + 25 * xyScale + brim_width)
        has_to_arrange = true;

    /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *print_config; //make a copy
    new_print_config.set_key_value("complete_objects", new ConfigOptionBool(true));
    //if skirt, use only one
    if (print_config->option<ConfigOptionInt>("skirts")->getInt() > 0 && print_config->option<ConfigOptionInt>("skirt_height")->getInt() > 0) {
        new_print_config.set_key_value("complete_objects_one_skirt", new ConfigOptionBool(true));
    }

    /// --- custom config ---
    for (size_t i = 0; i < 5; i++) {
        //brim to have some time to build up pressure in the nozzle
        model.objects[objs_idx[i]]->config.set_key_value("brim_width", new ConfigOptionFloat(brim_width));
        model.objects[objs_idx[i]]->config.set_key_value("external_perimeter_overlap", new ConfigOptionPercent(100));
        model.objects[objs_idx[i]]->config.set_key_value("perimeter_overlap", new ConfigOptionPercent(100));
        model.objects[objs_idx[i]]->config.set_key_value("brim_ears", new ConfigOptionBool(false));
        model.objects[objs_idx[i]]->config.set_key_value("perimeters", new ConfigOptionInt(3));
        model.objects[objs_idx[i]]->config.set_key_value("only_one_perimeter_top", new ConfigOptionBool(true));
        model.objects[objs_idx[i]]->config.set_key_value("enforce_full_fill_volume", new ConfigOptionBool(true));
        model.objects[objs_idx[i]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(5));
        model.objects[objs_idx[i]]->config.set_key_value("top_solid_layers", new ConfigOptionInt(100));
        model.objects[objs_idx[i]]->config.set_key_value("thin_walls", new ConfigOptionBool(true));
        model.objects[objs_idx[i]]->config.set_key_value("thin_walls_min_width", new ConfigOptionFloatOrPercent(50,true));
        model.objects[objs_idx[i]]->config.set_key_value("gap_fill_enabled", new ConfigOptionBool(true));
        model.objects[objs_idx[i]]->config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        model.objects[objs_idx[i]]->config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(first_layer_height, false));
        model.objects[objs_idx[i]]->config.set_key_value("external_infill_margin", new ConfigOptionFloatOrPercent(100, true));
        model.objects[objs_idx[i]]->config.set_key_value("solid_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        model.objects[objs_idx[i]]->config.set_key_value("top_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        //disable ironing post-process
        model.objects[objs_idx[i]]->config.set_key_value("ironing", new ConfigOptionBool(false));
        //set extrusion mult: 80 90 100 110 120
        model.objects[objs_idx[i]]->config.set_key_value("print_extrusion_multiplier", new ConfigOptionPercent(start + (float)i * delta));
    }

    //update plater
    GLCanvas3D::set_warning_freeze(false);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    plat->changed_objects(objs_idx);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->update_dirty();
    //update everything, easier to code.
    ObjectList* obj = this->gui_app->obj_list();
    obj->update_after_undo_redo();

    // arrange if needed, after new settings, to take them into account
    if (has_to_arrange) {
        //update print config (done at reslice but we need it here)
        if (plat->printer_technology() == ptFFF)
            plat->fff_print().apply(plat->model(), *plat->config());
        std::shared_ptr<ProgressIndicatorStub> fake_statusbar = std::make_shared<ProgressIndicatorStub>();
        ArrangeJob arranger(std::dynamic_pointer_cast<ProgressIndicator>(fake_statusbar), plat);
        arranger.prepare_all();
        arranger.process();
        arranger.finalize();
    }

    plat->reslice();

    if (autocenter) {
        //re-enable auto-center after this calibration.
        gui_app->app_config->set("autocenter", "1");
    }
}

} // namespace GUI
} // namespace Slic3r
