#include "CalibrationTempDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
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

void CalibrationTempDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxString choices_steps[] = { "5","10","15","20" };
    steps = new wxComboBox(this, wxID_ANY, wxString{ "10" }, wxDefaultPosition, wxDefaultSize, 4, choices_steps);
    steps->SetToolTip(_L("Select the step in celcius between two tests.\nNote that only multiple of 5 are engraved on the part."));
    steps->SetSelection(1);
    wxString choices_nb[] = { "0","1","2","3","4","5","6","7" };
    nb_down = new wxComboBox(this, wxID_ANY, wxString{ "2" }, wxDefaultPosition, wxDefaultSize, 8, choices_nb);
    nb_down->SetToolTip(_L("Select the number of tests with lower temperature than the current one."));
    nb_down->SetSelection(2);
    nb_up = new wxComboBox(this, wxID_ANY, wxString{ "2" }, wxDefaultPosition, wxDefaultSize, 8, choices_nb);
    nb_up->SetToolTip(_L("Select the number of tests with higher temperature than the current one."));
    nb_up->SetSelection(2);

    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Nb down:")));
    buttons->Add(nb_down);
    buttons->AddSpacer(15);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Nb up:")));
    buttons->Add(nb_up);
    buttons->AddSpacer(40);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Steps:")));
    buttons->Add(steps);
    buttons->AddSpacer(40);

    wxButton* bt = new wxButton(this, wxID_FILE1, _L("Generate"));
    bt->Bind(wxEVT_BUTTON, &CalibrationTempDialog::create_geometry, this);
    buttons->Add(bt);
}

void CalibrationTempDialog::create_geometry(wxCommandEvent& event_args) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    if (!plat->new_project(L("Temperature calibration")))
        return;

    GLCanvas3D::set_warning_freeze(true);
    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_temp" / "Smart_compact_temperature_calibration_item.amf").string()}, true, false, false);

    assert(objs_idx.size() == 1);
    const DynamicPrintConfig* print_config = this->gui_app->get_tab(Preset::TYPE_PRINT)->get_config();
    const DynamicPrintConfig* filament_config = this->gui_app->get_tab(Preset::TYPE_FILAMENT)->get_config();
    const DynamicPrintConfig* printer_config = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();

    // -- get temps
    const ConfigOptionInts* temperature_config = filament_config->option<ConfigOptionInts>("temperature");
    assert(temperature_config->values.size() >= 1);
    long nb_items_up = 1;
    if (!nb_up->GetValue().ToLong(&nb_items_up)) {
        nb_items_up = 2;
    }
    long nb_items_down = 1;
    if (!nb_down->GetValue().ToLong(&nb_items_down)) {
        nb_items_down = 2;
    }
    int16_t temperature = 5 * (temperature_config->values[0] / 5);
    long step_temp = 1;
    if (!steps->GetValue().ToLong(&step_temp)) {
        step_temp = 10;
    }
    size_t nb_items = 1 + nb_items_up + nb_items_down;
    //start at the highest temp
    temperature = temperature + step_temp * nb_items_up;
    
    /// --- scale ---
    //model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter_config = printer_config->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float xyzScale = nozzle_diameter / 0.4;
    //do scaling
    if (xyzScale < 0.9 || 1.1 < xyzScale) {
        model.objects[objs_idx[0]]->scale(xyzScale, xyzScale * 0.5, xyzScale);
    } else {
        xyzScale = 1;
        model.objects[objs_idx[0]]->scale(xyzScale, xyzScale * 0.5, xyzScale);
    }

    //add 8 others
    std::vector<ModelObject*>tower;
    tower.push_back(model.objects[objs_idx[0]]);
    float zshift = (1 - xyzScale) / 2;
    if (temperature > 175 && temperature < 290 && temperature%5==0) {
        tower.push_back(add_part(model.objects[objs_idx[0]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_temp" / ("t"+std::to_string(temperature)+".amf")).string(),
            Vec3d{ xyzScale * 5, - xyzScale * 2.5, zshift - xyzScale * 2.5}, Vec3d{ xyzScale, xyzScale, xyzScale * 0.43 }));
    }
    for (int16_t i = 1; i < nb_items; i++) {
        tower.push_back(add_part(model.objects[objs_idx[0]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_temp" / ("Smart_compact_temperature_calibration_item.amf")).string(),
            Vec3d{ 0,0, i * 10 * xyzScale }, Vec3d{ xyzScale, xyzScale * 0.5, xyzScale }));
        int sub_temp = temperature - i * step_temp;
        if (sub_temp > 175 && sub_temp < 290 && sub_temp % 5 == 0) {
            tower.push_back(add_part(model.objects[objs_idx[0]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_temp" / ("t" + std::to_string(sub_temp) + ".amf")).string(),
                Vec3d{ xyzScale * 5, -xyzScale * 2.5, xyzScale * (i * 10 - 2.5) }, Vec3d{ xyzScale, xyzScale, xyzScale * 0.43 }));
        }
    }


    /// --- translate ---
    bool autocenter = gui_app->app_config->get("autocenter") == "1";
    if (!autocenter) {
        const ConfigOptionPoints* bed_shape = printer_config->option<ConfigOptionPoints>("bed_shape");
        Vec2d bed_size = BoundingBoxf(bed_shape->values).size();
        Vec2d bed_min = BoundingBoxf(bed_shape->values).min;
        model.objects[objs_idx[0]]->translate({ bed_min.x() + bed_size.x() / 2, bed_min.y() + bed_size.y() / 2, 0 });
    }

    /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *print_config; //make a copy
    new_print_config.set_key_value("complete_objects", new ConfigOptionBool(false));
    
    /// -- generate the heat change gcode
    //std::string str_layer_gcode = "{if layer_num > 0 and layer_z  <= " + std::to_string(2 * xyzScale) + "}\nM104 S" + std::to_string(temperature - (int8_t)nb_delta * 5);
    //    double print_z, std::string gcode,int extruder, std::string color
    double firstChangeHeight = print_config->get_abs_value("first_layer_height", nozzle_diameter);
    //model.custom_gcode_per_print_z.gcodes.emplace_back(CustomGCode::Item{ firstChangeHeight + nozzle_diameter/2, CustomGCode::Type::Custom, -1, "", "M104 S" + std::to_string(temperature) + " ; ground floor temp tower set" });
    model.objects[objs_idx[0]]->config.set_key_value("print_temperature", new ConfigOptionInt(temperature));
    for (int16_t i = 1; i < nb_items; i++) {
        model.custom_gcode_per_print_z.gcodes.emplace_back(CustomGCode::Item{ (i * 10 * xyzScale), CustomGCode::Type::Custom , -1, "", "M104 S" + std::to_string(temperature - i * step_temp) + " ; floor " + std::to_string(i) + " of the temp tower set" });
        //str_layer_gcode += "\n{ elsif layer_z >= " + std::to_string(i * 10 * xyzScale) + " and layer_z <= " + std::to_string((1 + i * 10) * xyzScale) + " }\nM104 S" + std::to_string(temperature - (int8_t)nb_delta * 5 + i * 5);
    }
    //str_layer_gcode += "\n{endif}\n";
    //DynamicPrintConfig new_printer_config = *printerConfig; //make a copy
    //new_printer_config.set_key_value("layer_gcode", new ConfigOptionString(str_layer_gcode));

    /// --- custom config ---
    float brim_width = print_config->option<ConfigOptionFloat>("brim_width")->value;
    if (brim_width < nozzle_diameter * 8) {
        model.objects[objs_idx[0]]->config.set_key_value("brim_width", new ConfigOptionFloat(nozzle_diameter * 8));
    }
    model.objects[objs_idx[0]]->config.set_key_value("brim_ears", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("perimeters", new ConfigOptionInt(1));
    model.objects[objs_idx[0]]->config.set_key_value("extra_perimeters_overhangs", new ConfigOptionBool(true));
    model.objects[objs_idx[0]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(2));
    model.objects[objs_idx[0]]->config.set_key_value("top_solid_layers", new ConfigOptionInt(3)); 
    model.objects[objs_idx[0]]->config.set_key_value("gap_fill_enabled", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("thin_perimeters", new ConfigOptionBool(true));
    model.objects[objs_idx[0]]->config.set_key_value("layer_height", new ConfigOptionFloat(nozzle_diameter / 2));
    model.objects[objs_idx[0]]->config.set_key_value("fill_density", new ConfigOptionPercent(7));
    model.objects[objs_idx[0]]->config.set_key_value("solid_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
    model.objects[objs_idx[0]]->config.set_key_value("top_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
    //disable ironing post-process, it only slow down things
    model.objects[objs_idx[0]]->config.set_key_value("ironing", new ConfigOptionBool(false));

    //update plater
    GLCanvas3D::set_warning_freeze(false);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    //this->gui_app->get_tab(Preset::TYPE_PRINTER)->load_config(new_printer_config);
    //plat->on_config_change(new_printer_config);
    plat->changed_objects(objs_idx);
    this->gui_app->get_tab(Preset::TYPE_PRINT)->update_dirty();
    //this->gui_app->get_tab(Preset::TYPE_PRINTER)->update_dirty();
    plat->is_preview_shown();
    //update everything, easier to code.
    ObjectList* obj = this->gui_app->obj_list();
    obj->update_after_undo_redo();


    plat->reslice();
}

} // namespace GUI
} // namespace Slic3r
