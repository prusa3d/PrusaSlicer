#include "Slic3r/Domain/ConfigCommon.hpp"

#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/ConfigDefUtils.hpp"

namespace Slic3r::Domain {

// Define our own marking functions, the regular ones are not accessible in Domain.

static const std::string& L(const std::string& s) { return s; }

using PrinterTechnology::FFF;
using PrinterTechnology::SLA;

void init_common_fdm_sla_config_items(ConfigDefinitions& defs, const PrinterTechnology technology)
{
    using Locations = std::set<ConfigLocation>;

    constexpr auto sla_object{SLAConfigLocation::Object};
    constexpr auto sla_material{SLAConfigLocation::Material};
    constexpr auto fdm_tool{FDMConfigLocation::Tool};
    constexpr auto fdm_object{FDMConfigLocation::Object};
    constexpr auto fdm_volume{FDMConfigLocation::Volume};

    const ConfigLocation printer{
        technology == FFF
            ? ConfigLocation{FDMConfigLocation::Printer}
            : ConfigLocation{SLAConfigLocation::Printer}
    };

    const ConfigLocation print{
        technology == FFF
            ? ConfigLocation{FDMConfigLocation::Print}
            : ConfigLocation{SLAConfigLocation::Print}
    };

    ConfigItemDef* def = nullptr;

    def = defs.add("printer_technology", typeid(EnumWrapper));
    def->location = printer;
    def->label = L("Printer technology");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("Printer technology");
    get_enum_defs().push_back(std::make_unique<EnumValueDefs>(
        EnumValueDefs{
            {int(PrinterTechnology::FFF), "FFF", L("FFF")},
            {int(PrinterTechnology::SLA), "SLA", L("SLA")}
        }
    ));
    if (technology == SLA)
        def->init_fn = init_with(PrinterTechnology::SLA, get_enum_defs().back().get());
    else
        def->init_fn = init_with(PrinterTechnology::FFF, get_enum_defs().back().get());

    def = defs.add("bed_shape", typeid(std::vector<Vec2d>));
    def->location = printer;
    def->label = L("Bed shape");
    def->option_group = ConfigItemDef::OptionGroup::Printer_Bed_SizeAndCoordinates;
    def->full_width = true;
    def->category = ConfigItemDef::Category::Printer_Bed;
    def->gui_type = ConfigItemDef::GUIType::bed_shape;
    def->init_fn = init_with((std::vector<Domain::Vec2d>{{0., 0.}, { 200., 0. }, { 200., 200. }, { 0., 200. }}));

    def = defs.add("bed_custom_texture", typeid(std::string));
    def->location = printer;
    def->label = L("Bed custom texture");
    def->option_group = ConfigItemDef::OptionGroup::Printer_Bed_SizeAndCoordinates;
    def->full_width = true;
    def->category = ConfigItemDef::Category::Printer_Bed;
    def->gui_type = ConfigItemDef::GUIType::file_picker;
    def->init_fn = init_with("");

    def = defs.add("bed_custom_model", typeid(std::string));
    def->location = printer;
    def->label = L("Bed custom model");
    def->option_group = ConfigItemDef::OptionGroup::Printer_Bed_SizeAndCoordinates;
    def->full_width = true;
    def->category = ConfigItemDef::Category::Printer_Bed;
    def->gui_type = ConfigItemDef::GUIType::file_picker;
    def->init_fn = init_with("");

    def = defs.add("elefant_foot_compensation", typeid(double));
    if (technology == SLA) {
        def->location = printer;
        def->overrides_in = Locations{ sla_material, sla_object};
        def->option_group = ConfigItemDef::OptionGroup::Printer_General_Corrections;
    }
    if (technology == FFF) {
        def->location = FDMConfigLocation::Print;
        def->compatibility_rule = CompatibilityRule::Average;
        def->overrides_in = Locations{ fdm_tool, fdm_object, fdm_volume };
        def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    }
    def->label = L("Elephant foot compensation");
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("The first layer will be shrunk in the XY plane by the configured value "
                     "to compensate for the 1st layer squish aka an Elephant Foot effect.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.);

    def = defs.add("thumbnails", typeid(std::string));
    def->location = printer;
    if (technology == FFF) {
        def->label = L("G-code thumbnails");
    }
    else {
        def->label = L("Thumbnails");
    }
    def->option_group =  ConfigItemDef::OptionGroup::Printer_General_FirmwareGCode;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 6;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Picture sizes to be stored in .gcode / .bgcode and .sl1 / .sl1s files, in the following format: \"XxY/EXT, XxY/EXT, ...\"\n"
                     "Currently supported extensions are PNG, QOI and JPG.");
    def->init_fn = init_with("");
    def->full_width = true;

    def = defs.add("thumbnails_format", typeid(EnumWrapper));
    def->location = printer;
    def->label = L("Format of G-code thumbnails");
    def->category = ConfigItemDef::Category::Hidden;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Format of G-code thumbnails: PNG for best quality, JPG for smallest size, QOI for low memory firmware");
    def->init_fn = init_with(
        GCodeThumbnailsFormat::PNG,
        {{int(GCodeThumbnailsFormat::PNG), "PNG", "PNG"},
         {int(GCodeThumbnailsFormat::JPG), "JPG", "JPG"},
         {int(GCodeThumbnailsFormat::QOI), "QOI", "QOI"}}
    );

    def = defs.add("layer_height", typeid(double));
    def->location = print;
    if (technology == FFF)
        def->overrides_in = Locations{ fdm_object, fdm_volume };
    def->label = L("Layer height");
    def->option_group = ConfigItemDef::OptionGroup::Print_LayerSurfaces_LayerHeight;
    def->category = ConfigItemDef::Category::Print_LayersSurfaces;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("This setting controls the height (and thus the total number) of the slices/layers. "
                   "Thinner layers give better accuracy but take more time to print.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.3);

    def = defs.add("max_print_height", typeid(double));
    def->location = printer;
    def->label = L("Max print height");
    def->option_group = ConfigItemDef::OptionGroup::Printer_General_SizeClearances;
    def->category = ConfigItemDef::Category::Printer_General;
    def->order = 1;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Set this to the maximum height that can be reached by your extruder while printing.");
    def->units = {L("mm")};
    def->min = 0;
    def->max = 1200;
    def->init_fn = init_with(200.);

    def = defs.add("output_filename_format", typeid(std::string));
    def->location = print;
    def->label = L("Output filename format");
    def->option_group = ConfigItemDef::OptionGroup::Print_OutputOptions_OutputFile;
    def->category = ConfigItemDef::Category::Print_OutputOptions;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("You can use all configuration options as variables inside this template. "
                   "For example: [layer_height], [fill_density] etc. You can also use [timestamp], "
                   "[year], [month], [day], [hour], [minute], [second], [version], "
                   "[input_filename_base], [default_output_extension].");
    def->full_width = true;
    def->init_fn = init_with("[input_filename_base].gcode");

    def = defs.add("slice_closing_radius", typeid(double));
    def->location = print;
    if (technology == FFF)
        def->overrides_in = Locations{fdm_object, fdm_volume };
    else
        def->overrides_in = Locations{ sla_object };
    def->label = L("Slice gap closing radius");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_DimensionalAccuracy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 2;
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("Cracks smaller than 2x gap closing radius are being filled during the triangle mesh slicing. "
                     "The gap closing operation may reduce the final print resolution, therefore it is advisable to keep the value reasonably low.");
    def->units = {L("mm")};
    def->min = 0;
    def->init_fn = init_with(0.049);

    def = defs.add("slicing_mode", typeid(EnumWrapper));
    def->location = print;
    if (technology == FFF) {
        def->overrides_in = Locations{ fdm_object };
    } else {
        def->overrides_in = Locations{ sla_object };
    }
    def->label = L("Slicing Mode");
    def->option_group = ConfigItemDef::OptionGroup::Print_PrecisionSlicing_SlicingStrategy;
    def->category = ConfigItemDef::Category::Print_PrecisionSlicing;
    def->order = 0;
    def->gui_type = ConfigItemDef::GUIType::combobox;
    def->tooltip = L("Use \"Even-odd\" for 3DLabPrint airplane models. Use \"Close holes\" to close all holes in the model.");
    def->init_fn = init_with(
        SlicingMode::Regular,
        {{int(SlicingMode::Regular), "regular", L("Regular")},
         {int(SlicingMode::EvenOdd), "even_odd", L("Even-odd")},
         {int(SlicingMode::CloseHoles), "close_holes", L("Close holes")}}
    );

    def = defs.add("printer_model", typeid(std::string));
    def->location = printer;
    def->label = L("Printer type");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("Type of the printer.");
    def->init_fn = init_with("");

    def = defs.add("printer_variant", typeid(std::string));
    def->location = printer;
    def->label = L("Printer variant");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("Name of the printer variant. For example, the printer variants may be differentiated by a nozzle diameter.");
    def->init_fn = init_with("");

    def = defs.add("printer_notes", typeid(std::string));
    def->location = printer;
    def->label = L("Printer notes");
    def->category = ConfigItemDef::Category::Printer_Notes;
    def->option_group = ConfigItemDef::OptionGroup::Printer_Notes_Notes; 
    def->gui_type = ConfigItemDef::GUIType::textfield;
    def->tooltip = L("You can put your notes regarding the printer here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->init_fn = init_with("");

    def = defs.add("default_print", typeid(std::string));
    def->location = printer;
    def->label = L("Default Print Preset");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("Name or ID of print preset to use as default when this printer preset is selected.");
    def->init_fn = init_with("");

    def = defs.add("default_material", typeid(std::string));
    def->location = print;
    def->label = L("Default Material Preset");
    def->category = ConfigItemDef::Category::Hidden;
    def->tooltip = L("Name or ID of material preset to use as default when this print preset is selected.");
    def->init_fn = init_with("");
}

}
