#include "CalibrationOverBridgeDialog.hpp"
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

void CalibrationOverBridgeDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxButton* bt1 = new wxButton(this, wxID_FILE1, _L("Over-Bridge calibration"));
    wxButton* bt2 = new wxButton(this, wxID_FILE1, _L("Top flow calibration"));
    bt1->Bind(wxEVT_BUTTON, &CalibrationOverBridgeDialog::create_geometry1, this);
    bt2->Bind(wxEVT_BUTTON, &CalibrationOverBridgeDialog::create_geometry2, this);
    buttons->Add(bt1);
    buttons->Add(bt2);
}

void CalibrationOverBridgeDialog::create_geometry1(wxCommandEvent& event_args) {
    create_geometry(true);
}
void CalibrationOverBridgeDialog::create_geometry2(wxCommandEvent& event_args) {
    create_geometry(false);
}
void CalibrationOverBridgeDialog::create_geometry(bool over_bridge) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    if (!plat->new_project(L("Over-bridge calibration")))
        return;

    GLCanvas3D::set_warning_freeze(true);
    bool autocenter = gui_app->app_config->get("autocenter") == "1";
    if (autocenter) {
        //disable aut-ocenter for this calibration.
        gui_app->app_config->set("autocenter", "0");
    }

    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "over-bridge_tuning" / "over-bridge_flow_ratio_test.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "over-bridge_tuning" / "over-bridge_flow_ratio_test.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "over-bridge_tuning" / "over-bridge_flow_ratio_test.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "over-bridge_tuning" / "over-bridge_flow_ratio_test.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "over-bridge_tuning" / "over-bridge_flow_ratio_test.amf").string(),
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "over-bridge_tuning" / "over-bridge_flow_ratio_test.amf").string()}, true, false, false);

    assert(objs_idx.size() == 6);
    const DynamicPrintConfig* print_config = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();
    const DynamicPrintConfig* printer_config = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();

    /// --- scale ---
    // model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter_config = printer_config->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float xyz_scale = (0.2 + nozzle_diameter) / 0.6;
    //do scaling
    if (xyz_scale < 0.9 || 1.2 < xyz_scale) {
    } else {
        xyz_scale = 1;
    }
    for (size_t i = 0; i < 6; i++)
        model.objects[objs_idx[i]]->scale(xyz_scale * 1.5f, xyz_scale * 1.5f, xyz_scale);

    //add sub-part after scale
    const ConfigOptionFloatOrPercent* first_layer_height = print_config->option<ConfigOptionFloatOrPercent>("first_layer_height");
    float patch_zscale = (first_layer_height->get_abs_value(nozzle_diameter) + nozzle_diameter / 2) / 0.4;
    float zshift =  0.8 * (1 - xyz_scale);
    for (size_t i = 0; i < 6; i++) {
        model.objects[objs_idx[i]]->rotate(PI / 2, { 0,0,1 });
        ModelObject* obj = add_part(model.objects[objs_idx[i]], (boost::filesystem::path(Slic3r::resources_dir()) /"calibration" / "bridge_flow" / ("f"+std::to_string(100 + i * 5)+".amf")).string(), Vec3d{ 0, 10 * xyz_scale ,zshift }, Vec3d{ 1, 1, patch_zscale });
    }

    /// --- translate ---;
    bool has_to_arrange = false;
    const ConfigOptionFloat* extruder_clearance_radius = print_config->option<ConfigOptionFloat>("extruder_clearance_radius");
    const ConfigOptionPoints* bed_shape = printer_config->option<ConfigOptionPoints>("bed_shape");
    const float brim_width = print_config->option<ConfigOptionFloat>("brim_width")->getFloat();
    Vec2d bed_size = BoundingBoxf(bed_shape->values).size();
    Vec2d bed_min = BoundingBoxf(bed_shape->values).min;
    float offsetx = 3 + 30 * xyz_scale + extruder_clearance_radius->value + brim_width + (brim_width > extruder_clearance_radius->value ? brim_width - extruder_clearance_radius->value : 0);
    float offsety = 3 + 25 * xyz_scale + extruder_clearance_radius->value + brim_width + (brim_width > extruder_clearance_radius->value ? brim_width - extruder_clearance_radius->value : 0);
    model.objects[objs_idx[0]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx / 2, bed_min.y() + bed_size.y() / 2 - offsety, 0 });
    model.objects[objs_idx[1]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx / 2, bed_min.y() + bed_size.y() / 2          , 0 });
    model.objects[objs_idx[2]]->translate({ bed_min.x() + bed_size.x() / 2 - offsetx / 2, bed_min.y() + bed_size.y() / 2 + offsety, 0 });
    model.objects[objs_idx[3]]->translate({ bed_min.x() + bed_size.x() / 2 + offsetx / 2, bed_min.y() + bed_size.y() / 2 - offsety, 0 });
    model.objects[objs_idx[4]]->translate({ bed_min.x() + bed_size.x() / 2 + offsetx / 2, bed_min.y() + bed_size.y() / 2          , 0 });
    model.objects[objs_idx[5]]->translate({ bed_min.x() + bed_size.x() / 2 + offsetx / 2, bed_min.y() + bed_size.y() / 2 + offsety, 0 });

    // if not enough space, forget about complete_objects
    if (bed_size.y() < offsety * 2 + 30 * xyz_scale + brim_width || bed_size.x() < offsetx + 35 * xyz_scale + brim_width)
        has_to_arrange = true;

    /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *print_config; //make a copy
    new_print_config.set_key_value("complete_objects", new ConfigOptionBool(true));
    //if skirt, use only one
    if (print_config->option<ConfigOptionInt>("skirts")->getInt() > 0 && print_config->option<ConfigOptionInt>("skirt_height")->getInt() > 0) {
        new_print_config.set_key_value("complete_objects_one_skirt", new ConfigOptionBool(true));
    }

    /// --- custom config ---
    for (size_t i = 0; i < 6; i++) {
        model.objects[objs_idx[i]]->config.set_key_value("perimeters", new ConfigOptionInt(2));
        model.objects[objs_idx[i]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(0));
        model.objects[objs_idx[i]]->config.set_key_value("top_solid_layers", new ConfigOptionInt(3));
        model.objects[objs_idx[i]]->config.set_key_value("fill_density", new ConfigOptionPercent(5.5));
        model.objects[objs_idx[i]]->config.set_key_value("fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        model.objects[objs_idx[i]]->config.set_key_value("infill_dense", new ConfigOptionBool(false));
        model.objects[objs_idx[i]]->config.set_key_value("ironing", new ConfigOptionBool(false));
        //calibration setting. Use 100 & 5 step as it's the numbers printed on the samples
        if(over_bridge)
            model.objects[objs_idx[i]]->config.set_key_value("over_bridge_flow_ratio", new ConfigOptionPercent(/*print_config->option<ConfigOptionPercent>("over_bridge_flow_ratio")->get_abs_value(100)*/100 + i * 5));
        else
            model.objects[objs_idx[i]]->config.set_key_value("fill_top_flow_ratio", new ConfigOptionPercent(/*print_config->option<ConfigOptionPercent>("fill_top_flow_ratio")->get_abs_value(100)*/100 + i * 5));
        model.objects[objs_idx[i]]->config.set_key_value("layer_height", new ConfigOptionFloat(nozzle_diameter / 2));
        model.objects[objs_idx[i]]->config.set_key_value("external_infill_margin", new ConfigOptionFloatOrPercent(400,true));
        model.objects[objs_idx[i]]->config.set_key_value("top_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        model.objects[objs_idx[i]]->config.set_key_value("fill_angle", new ConfigOptionFloat(45));
    }

    //update plater
    GLCanvas3D::set_warning_freeze(false);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    plat->changed_objects(objs_idx);
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
