#include "CalibrationBridgeDialog.hpp"
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

void CalibrationBridgeDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxString choices_steps[] = { "5","10","15" };
    steps = new wxComboBox(this, wxID_ANY, wxString{ "10" }, wxDefaultPosition, wxDefaultSize, 3, choices_steps);
    steps->SetToolTip(_L("Select the step in % between two tests.\nNote that only multiple of 5 are engraved on the parts."));
    steps->SetSelection(1);
    wxString choices_nb[] = { "1","2","3","4","5","6" };
    nb_tests = new wxComboBox(this, wxID_ANY, wxString{ "5" }, wxDefaultPosition, wxDefaultSize, 6, choices_nb);
    nb_tests->SetToolTip(_L("Select the number of tests"));
    nb_tests->SetSelection(4);

    buttons->Add(new wxStaticText(this, wxID_ANY,_L("Step:")));
    buttons->Add(steps);
    buttons->AddSpacer(15);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Nb tests:")));
    buttons->Add(nb_tests);
    buttons->AddSpacer(40);
    wxButton* bt = new wxButton(this, wxID_FILE1, _L("Test Flow Ratio"));
    bt->Bind(wxEVT_BUTTON, &CalibrationBridgeDialog::create_geometry_flow_ratio, this);
    buttons->Add(bt);
    //buttons->AddSpacer(15);
    //bt = new wxButton(this, wxID_FILE1, _(L("Test Overlap")));
    //bt->Bind(wxEVT_BUTTON, &CalibrationBridgeDialog::create_geometry_overlap, this);
    //buttons->Add(bt);
}

void CalibrationBridgeDialog::create_geometry(std::string setting_to_test, bool add) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    if (!plat->new_project(L("Bridge calibration")))
        return;

    GLCanvas3D::set_warning_freeze(true);
    bool autocenter = gui_app->app_config->get("autocenter") == "1";
    if (autocenter) {
        //disable auto-center for this calibration.
        gui_app->app_config->set("autocenter", "0");
    }

    long step = 10;
    if (!steps->GetValue().ToLong(&step)) {
        step = 10;
    }
    long nb_items = 10;
    if (!nb_tests->GetValue().ToLong(&nb_items)) {
        nb_items = 10;
    }

    std::vector<std::string> items;
    for (size_t i = 0; i < nb_items; i++)
        items.emplace_back((boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "bridge_flow" / "bridge_test.amf").string());
    std::vector<size_t> objs_idx = plat->load_files(items, true, false, false);

    assert(objs_idx.size() == nb_items);
    const DynamicPrintConfig* print_config = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();
    const DynamicPrintConfig* filament_config = this->gui_app->get_tab(Preset::TYPE_FILAMENT)->get_config();
    const DynamicPrintConfig* printer_config = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();
    DynamicPrintConfig full_print_config;
    full_print_config.apply(*print_config);
    full_print_config.apply(*filament_config);
    full_print_config.apply(*printer_config);
    full_print_config.set_key_value("extruder_id", new ConfigOptionInt(0));

    /// --- scale ---
    // model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter_config = printer_config->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float z_scale = nozzle_diameter / 0.4;
    //do scaling
    if (z_scale < 0.9 || 1.2 < z_scale) {
        for (size_t i = 0; i < 5; i++)
            model.objects[objs_idx[i]]->scale(1, 1, z_scale);
    } else {
        z_scale = 1;
    }

    //add sub-part after scale
    const ConfigOptionPercent* bridge_flow_ratio = print_config->option<ConfigOptionPercent>(setting_to_test);
    int start = bridge_flow_ratio->value;
    float zshift = 2.3 * (1 - z_scale);
    for (size_t i = 0; i < nb_items; i++) {
        int step_num = (start + (add ? 1 : -1) * i * step);
        if (step_num < 180 && step_num > 20 && step_num%5 == 0) {
            add_part(model.objects[objs_idx[i]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "bridge_flow" / ("f" + std::to_string(step_num) + ".amf")).string(), Vec3d{ -10,0, zshift + 4.6 * z_scale }, Vec3d{ 1,1,z_scale });
        }
    }
    /// --- translate ---;
    bool has_to_arrange = false;
    const float brim_width = std::max(print_config->option<ConfigOptionFloat>("brim_width")->value, nozzle_diameter * 5.);
    const ConfigOptionFloat* extruder_clearance_radius = print_config->option<ConfigOptionFloat>("extruder_clearance_radius");
    const ConfigOptionPoints* bed_shape = printer_config->option<ConfigOptionPoints>("bed_shape");
    Vec2d bed_size = BoundingBoxf(bed_shape->values).size();
    Vec2d bed_min = BoundingBoxf(bed_shape->values).min;
    float offsety = 2 + 10 * 1 + extruder_clearance_radius->value + brim_width + (brim_width > extruder_clearance_radius->value ? brim_width - extruder_clearance_radius->value : 0);
    model.objects[objs_idx[0]]->translate({ bed_min.x() + bed_size.x() / 2, bed_min.y() + bed_size.y() / 2, 0 });
    for (int i = 1; i < nb_items; i++) {
        model.objects[objs_idx[i]]->translate({ bed_min.x() + bed_size.x() / 2, bed_min.y() + bed_size.y() / 2 + (i % 2 == 0 ? -1 : 1) * offsety * ((i + 1) / 2), 0 });
    }
    // if not enough space, forget about complete_objects
    if (bed_size.y() < offsety * (nb_items + 1))
        has_to_arrange = true;


    /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *print_config; //make a copy
    new_print_config.set_key_value("complete_objects", new ConfigOptionBool(true));
    //if skirt, use only one
    if (print_config->option<ConfigOptionInt>("skirts")->getInt() > 0 && print_config->option<ConfigOptionInt>("skirt_height")->getInt() > 0) {
        new_print_config.set_key_value("complete_objects_one_skirt", new ConfigOptionBool(true));
    }

    /// --- custom config ---
    for (size_t i = 0; i < nb_items; i++) {
        model.objects[objs_idx[i]]->config.set_key_value("brim_width", new ConfigOptionFloat(brim_width));
        model.objects[objs_idx[i]]->config.set_key_value("brim_ears", new ConfigOptionBool(false));
        model.objects[objs_idx[i]]->config.set_key_value("perimeters", new ConfigOptionInt(2));
        model.objects[objs_idx[i]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(2));
        model.objects[objs_idx[i]]->config.set_key_value("gap_fill_enabled", new ConfigOptionBool(false));
        model.objects[objs_idx[i]]->config.set_key_value(setting_to_test, new ConfigOptionPercent(start + (add ? 1 : -1) * i * step));
        model.objects[objs_idx[i]]->config.set_key_value("layer_height", new ConfigOptionFloat(nozzle_diameter / 2));
        //model.objects[objs_idx[i]]->config.set_key_value("no_perimeter_unsupported_algo", new ConfigOptionEnum<NoPerimeterUnsupportedAlgo>(npuaBridges));
        //model.objects[objs_idx[i]]->config.set_key_value("top_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipSmooth)); /not needed
        model.objects[objs_idx[i]]->config.set_key_value("ironing", new ConfigOptionBool(false)); // not needed, and it slow down things.
    }
    /// if first ayer height is excactly at the wrong value, the text isn't drawed. Fix that by switching the first layer height just a little bit.
    // double first_layer_height = full_print_config.get_computed_value("first_layer_height", 0);
    auto *first_layer_height_opt = full_print_config.option<ConfigOptionFloatOrPercent>("first_layer_height", false);
    double first_layer_height = first_layer_height_opt->value;
  
    double layer_height = nozzle_diameter * 0.5;
    if (layer_height > 0.01 && (int(first_layer_height * 100) % int(layer_height * 100)) == int(layer_height * 50)) {
        double z_step = printer_config->option<ConfigOptionFloat>("z_step")->value;
        if (z_step == 0)
            z_step = 0.1;
        //double max_height = full_print_config.get_computed_value("max_layer_height",0);
        auto *max_layer_height_opt = full_print_config.option<ConfigOptionFloatOrPercent>("max_layer_height", false);
        double max_height = first_layer_height_opt->value;
        if (max_height > first_layer_height + z_step)
            for (size_t i = 0; i < nb_items; i++)
                model.objects[objs_idx[i]]->config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(first_layer_height + z_step, false));
        else
            for (size_t i = 0; i < nb_items; i++)
                model.objects[objs_idx[i]]->config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(first_layer_height - z_step, false));
    }

    //update plater
    GLCanvas3D::set_warning_freeze(false);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    plat->changed_objects(objs_idx);
    //this->gui_app->get_tab(Preset::TYPE_PRINT)->update_dirty();
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
