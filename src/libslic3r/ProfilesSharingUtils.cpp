///|/ Copyright (c) Prusa Research 2021 - 2023 Oleksandra Iushchenko @YuSanka
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ProfilesSharingUtils.hpp"
#include "Utils.hpp"
#include "format.hpp"
#include "PrintConfig.hpp"
#include "PresetBundle.hpp"
#include "Utils/DirectoriesUtils.hpp"
#include "Utils/JsonUtils.hpp"
#include "BuildVolume.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r {

static bool load_preset_bundle_from_datadir(PresetBundle& preset_bundle)
{
    AppConfig app_config = AppConfig(AppConfig::EAppMode::Editor);
    if (!app_config.exists()) {
        BOOST_LOG_TRIVIAL(error) << "Configuration wasn't found. Check your 'datadir' value.";
        return false;
    }

    if (std::string error = app_config.load(); !error.empty()) {
        BOOST_LOG_TRIVIAL(error) << Slic3r::format("Error parsing PrusaSlicer config file, it is probably corrupted. "
            "Try to manually delete the file to recover from the error. Your user profiles will not be affected."
            "\n%1%\n%2%", app_config.config_path(), error);
        return false;
    }

    // just checking for existence of Slic3r::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    preset_bundle.setup_directories();

    std::string delayed_error_load_presets;
    // Suppress the '- default -' presets.
    preset_bundle.set_default_suppressed(app_config.get_bool("no_defaults"));
    try {
        auto preset_substitutions = preset_bundle.load_presets(app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        if (!preset_substitutions.empty()) {
            BOOST_LOG_TRIVIAL(error) << "Some substitutions are found during loading presets.";
            return false;
        }

        // Post-process vendor map to delete non-installed models/varians

        VendorMap& vendors = preset_bundle.vendors;
        for (auto& [vendor_id, vendor_profile] : vendors) {
            std::vector<VendorProfile::PrinterModel> models;

            for (auto& printer_model : vendor_profile.models) {
                std::vector<VendorProfile::PrinterVariant> variants;

                for (const auto& variant : printer_model.variants) {
                    // check if printer model with variant is intalled
                    if (app_config.get_variant(vendor_id, printer_model.id, variant.name))
                        variants.push_back(variant);
                }

                if (!variants.empty()) {
                    if (printer_model.variants.size() != variants.size())
                        printer_model.variants = variants;
                    models.push_back(printer_model);
                }
            }

            if (!models.empty()) {
                if (vendor_profile.models.size() != models.size())
                    vendor_profile.models = models;
            }
        }
    }
    catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << ex.what();
        return false;
    }

    return true;
}

namespace pt = boost::property_tree;
/*
struct PrinterAttr_
{
    std::string         model_name;
    std::string         variant;
};

static std::string get_printer_profiles(const VendorProfile* vendor_profile, 
                                        const PresetBundle* preset_bundle, 
                                        const PrinterAttr_& printer_attr)
{
    for (const auto& printer_model : vendor_profile->models) {
        if (printer_model.name != printer_attr.model_name)
            continue;

        for (const auto& variant : printer_model.variants)
            if (variant.name == printer_attr.variant)
            {
                pt::ptree data_node;
                data_node.put("printer_model", printer_model.name);
                data_node.put("printer_variant", printer_attr.variant);

                pt::ptree printer_profiles_node;
                for (const Preset& printer_preset : preset_bundle->printers) {
                    if (printer_preset.vendor->id == vendor_profile->id &&
                        printer_preset.is_visible && // ???
                        printer_preset.config.opt_string("printer_model") == printer_model.id &&
                        printer_preset.config.opt_string("printer_variant") == printer_attr.variant) {
                        pt::ptree profile_node;
                        profile_node.put("", printer_preset.name);
                        printer_profiles_node.push_back(std::make_pair("", profile_node));
                    }
                }
                data_node.add_child("printer_profiles", printer_profiles_node);

                // Serialize the tree into JSON and return it.
                return write_json_with_post_process(data_node);
            }
    }

    return "";
}

std::string get_json_printer_profiles(const std::string& printer_model_name, const std::string& printer_variant)
{
    if (!is_datadir())
        return "";

    PrinterAttr_ printer_attr({printer_model_name, printer_variant});

    PresetBundle preset_bundle;
    if (!load_preset_bundle_from_datadir(preset_bundle))
        return "";

    const VendorMap& vendors = preset_bundle.vendors;
    for (const auto& [vendor_id, vendor] : vendors) {
        std::string out = get_printer_profiles(&vendor, &preset_bundle, printer_attr);
        if (!out.empty())
            return out;
    }

    return "";
}
*/

struct PrinterAttr
{
    std::string vendor_id;
    std::string model_id;
    std::string variant_name;
};

static bool is_compatible_preset(const Preset& printer_preset, const PrinterAttr& attr)
{
    return  printer_preset.vendor->id == attr.vendor_id &&
            printer_preset.config.opt_string("printer_model") == attr.model_id &&
            printer_preset.config.opt_string("printer_variant") == attr.variant_name;
}

static void add_profile_node(pt::ptree& printer_profiles_node, const Preset& printer_preset)
{
    pt::ptree profile_node;

    const DynamicPrintConfig& config = printer_preset.config;

    int extruders_cnt = printer_preset.printer_technology() == ptSLA ? 0 :
                        config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();

    profile_node.put("name", printer_preset.name);
    if (extruders_cnt > 0)
        profile_node.put("extruders_cnt", extruders_cnt);

    const double max_print_height = config.opt_float("max_print_height");
    const ConfigOptionPoints& bed_shape = *config.option<ConfigOptionPoints>("bed_shape");

    BuildVolume build_volume = BuildVolume { bed_shape.values, max_print_height };
    BoundingBoxf        bb   = build_volume.bounding_volume2d();

    Vec2d origin_pt;
    if (build_volume.type() == BuildVolume::Type::Circle) {
        origin_pt = build_volume.bed_center();
    }
    else {
        origin_pt = to_2d(-1 * build_volume.bounding_volume().min);
    }
    std::string origin = Slic3r::format("[%1%, %2%]", is_approx(origin_pt.x(), 0.) ? 0 : origin_pt.x(),
                                                      is_approx(origin_pt.y(), 0.) ? 0 : origin_pt.y());

    pt::ptree bed_node;
    bed_node.put("type",                build_volume.type_name());
    bed_node.put("width",               bb.max.x() - bb.min.x());
    bed_node.put("height",              bb.max.y() - bb.min.y());
    bed_node.put("origin",              origin);
    bed_node.put("max_print_height",    max_print_height);

    profile_node.add_child("bed", bed_node);

    printer_profiles_node.push_back(std::make_pair("", profile_node));
}

static void get_printer_profiles_node(pt::ptree& printer_profiles_node, 
                                      pt::ptree& user_printer_profiles_node,
                                      const PrinterPresetCollection& printer_presets,
                                      const PrinterAttr& attr)
{
    printer_profiles_node.clear();
    user_printer_profiles_node.clear();

    for (const Preset& printer_preset : printer_presets) {
        if (!printer_preset.is_visible)
            continue;

        if (printer_preset.is_user()) {
            const Preset* parent_preset = printer_presets.get_preset_parent(printer_preset);
            if (parent_preset && is_compatible_preset(*parent_preset, attr))
                add_profile_node(user_printer_profiles_node, printer_preset);
        }
        else if (is_compatible_preset(printer_preset, attr))
            add_profile_node(printer_profiles_node, printer_preset);
    }
}

static void add_printer_models(pt::ptree& vendor_node,
                               const VendorProfile* vendor_profile,
                               PrinterTechnology printer_technology,
                               const PrinterPresetCollection& printer_presets)
{
    for (const auto& printer_model : vendor_profile->models) {
        if (printer_technology != ptUnknown && printer_model.technology != printer_technology)
            continue;

        pt::ptree variants_node;
        pt::ptree printer_profiles_node;
        pt::ptree user_printer_profiles_node;

        if (printer_model.technology == ptSLA) {
            PrinterAttr attr({ vendor_profile->id, printer_model.id, "default" });

            get_printer_profiles_node(printer_profiles_node, user_printer_profiles_node, printer_presets, attr);
            if (printer_profiles_node.empty() && user_printer_profiles_node.empty())
                continue;
        }
        else {
            for (const auto& variant : printer_model.variants) {

                PrinterAttr attr({ vendor_profile->id, printer_model.id, variant.name });

                get_printer_profiles_node(printer_profiles_node, user_printer_profiles_node, printer_presets, attr);
                if (printer_profiles_node.empty() && user_printer_profiles_node.empty())
                    continue;

                pt::ptree variant_node;
                variant_node.put("name", variant.name);
                variant_node.add_child("printer_profiles", printer_profiles_node);
                if (!user_printer_profiles_node.empty())
                    variant_node.add_child("user_printer_profiles", user_printer_profiles_node);

                variants_node.push_back(std::make_pair("", variant_node));
            }

            if (variants_node.empty())
                continue;
        }

        pt::ptree data_node;
        data_node.put("id", printer_model.id);
        data_node.put("name", printer_model.name);
        data_node.put("technology", printer_model.technology == ptFFF ? "FFF" : "SLA");

        if (!variants_node.empty())
            data_node.add_child("variants", variants_node);
        else {
            data_node.add_child("printer_profiles", printer_profiles_node);
            if (!user_printer_profiles_node.empty())
                data_node.add_child("user_printer_profiles", user_printer_profiles_node);
        }

        data_node.put("vendor_name", vendor_profile->name);
        data_node.put("vendor_id", vendor_profile->id);

        vendor_node.push_back(std::make_pair("", data_node));
    }
}

static void add_undef_printer_models(pt::ptree& vendor_node,
                                     PrinterTechnology printer_technology,
                                     const PrinterPresetCollection& printer_presets)
{
    for (auto pt : { ptFFF, ptSLA }) {
        if (printer_technology != ptUnknown && printer_technology != pt)
            continue;

        pt::ptree printer_profiles_node;
        for (const Preset& preset : printer_presets) {
            if (!preset.is_visible || preset.printer_technology() != pt ||
                preset.vendor || printer_presets.get_preset_parent(preset))
                continue;

            add_profile_node(printer_profiles_node, preset);
        }

        if (!printer_profiles_node.empty()) {
            pt::ptree data_node;
            data_node.put("id", "");
            data_node.put("technology", pt == ptFFF ? "FFF" : "SLA");
            data_node.add_child("printer_profiles", printer_profiles_node);
            data_node.put("vendor_name", "");
            data_node.put("vendor_id", "");

            vendor_node.push_back(std::make_pair("", data_node));
        }
    }
}

std::string get_json_printer_models(PrinterTechnology printer_technology)
{
    PresetBundle preset_bundle;
    if (!load_preset_bundle_from_datadir(preset_bundle))
        return "";
            
    pt::ptree vendor_node;

    const VendorMap& vendors_map = preset_bundle.vendors;
    for (const auto& [vendor_id, vendor] : vendors_map)
        add_printer_models(vendor_node, &vendor, printer_technology, preset_bundle.printers);

    // add printers with no vendor information
    add_undef_printer_models(vendor_node, printer_technology, preset_bundle.printers);

    pt::ptree root;
    root.add_child("printer_models", vendor_node);

    // Serialize the tree into JSON and return it.
    return write_json_with_post_process(root);
}

static std::string get_installed_print_and_filament_profiles(const PresetBundle* preset_bundle, const Preset* printer_preset)
{
    PrinterTechnology printer_technology = printer_preset->printer_technology();

    pt::ptree print_profiles;
    pt::ptree user_print_profiles;

    const PresetWithVendorProfile printer_preset_with_vendor_profile = preset_bundle->printers.get_preset_with_vendor_profile(*printer_preset);

    const PresetCollection& print_presets       = printer_technology == ptFFF ? preset_bundle->prints    : preset_bundle->sla_prints;
    const PresetCollection& material_presets    = printer_technology == ptFFF ? preset_bundle->filaments : preset_bundle->sla_materials;
    const std::string       material_node_name  = printer_technology == ptFFF ? "filament_profiles"      : "sla_material_profiles";

    for (auto print_preset : print_presets) {

        const PresetWithVendorProfile print_preset_with_vendor_profile = print_presets.get_preset_with_vendor_profile(print_preset);

        if (is_compatible_with_printer(print_preset_with_vendor_profile, printer_preset_with_vendor_profile))
        {
            pt::ptree materials_profile_node;
            pt::ptree user_materials_profile_node;

            for (auto material_preset : material_presets) {

                // ?! check visible and no-template presets only
                if (!material_preset.is_visible || (material_preset.vendor && material_preset.vendor->templates_profile))
                    continue;

                const PresetWithVendorProfile material_preset_with_vendor_profile = material_presets.get_preset_with_vendor_profile(material_preset);

                if (is_compatible_with_printer(material_preset_with_vendor_profile, printer_preset_with_vendor_profile) &&
                    is_compatible_with_print(material_preset_with_vendor_profile, print_preset_with_vendor_profile, printer_preset_with_vendor_profile)) {
                    pt::ptree material_node;
                    material_node.put("", material_preset.name);
                    if (material_preset.is_user())
                        user_materials_profile_node.push_back(std::make_pair("", material_node));
                    else
                        materials_profile_node.push_back(std::make_pair("", material_node));
                }
            }

            pt::ptree print_profile_node;
            print_profile_node.put("name", print_preset.name);
            print_profile_node.add_child(material_node_name, materials_profile_node);
            if (!user_materials_profile_node.empty())
                print_profile_node.add_child("user_" + material_node_name, user_materials_profile_node);

            if (print_preset.is_user())
                user_print_profiles.push_back(std::make_pair("", print_profile_node));
            else
                print_profiles.push_back(std::make_pair("", print_profile_node));
        }
    }

    if (print_profiles.empty() && user_print_profiles.empty())
        return "";

    pt::ptree tree;
    tree.put("printer_profile", printer_preset->name);
    tree.add_child("print_profiles", print_profiles);
    if (!user_print_profiles.empty())
        tree.add_child("user_print_profiles", user_print_profiles);

    // Serialize the tree into JSON and return it.
    return write_json_with_post_process(tree);
}

std::string get_json_print_filament_profiles(const std::string& printer_profile)
{
    PresetBundle preset_bundle;
    if (load_preset_bundle_from_datadir(preset_bundle)) {
        const Preset* preset = preset_bundle.printers.find_preset(printer_profile, false, false);
        if (preset)
            return get_installed_print_and_filament_profiles(&preset_bundle, preset);
    }

    return "";
}

// Helper function for FS
bool load_full_print_config(const std::string& print_preset_name, const std::string& filament_preset_name, const std::string& printer_preset_name, DynamicPrintConfig& config)
{
    PresetBundle preset_bundle;
    if (!load_preset_bundle_from_datadir(preset_bundle)){
        BOOST_LOG_TRIVIAL(error) << Slic3r::format("Failed to load data from the datadir '%1%'.", data_dir());
        return false;
    }

    config = {};
    config.apply(FullPrintConfig::defaults());

    bool is_failed{ false };

    if (const Preset* print_preset = preset_bundle.prints.find_preset(print_preset_name))
        config.apply_only(print_preset->config, print_preset->config.keys());
    else {
        BOOST_LOG_TRIVIAL(warning) << Slic3r::format("Print profile '%1%' wasn't found.", print_preset_name);
        is_failed |= true;
    }

    if (const Preset* filament_preset = preset_bundle.filaments.find_preset(filament_preset_name))
        config.apply_only(filament_preset->config, filament_preset->config.keys());
    else {
        BOOST_LOG_TRIVIAL(warning) << Slic3r::format("Filament profile '%1%' wasn't found.", filament_preset_name);
        is_failed |= true;
    }

    if (const Preset* printer_preset = preset_bundle.printers.find_preset(printer_preset_name))
        config.apply_only(printer_preset->config, printer_preset->config.keys());
    else {
        BOOST_LOG_TRIVIAL(warning) << Slic3r::format("Printer profile '%1%' wasn't found.", printer_preset_name);
        is_failed |= true;
    }

    return !is_failed;
}

// Helper function for load full config from installed presets by profile names
std::string load_full_print_config(const std::string& print_preset_name, 
                                   const std::vector<std::string>& material_preset_names_in, 
                                   const std::string& printer_preset_name,
                                   DynamicPrintConfig& config,
                                   PrinterTechnology printer_technology /*= ptUnknown*/)
{
    // check entered profile names

    if (print_preset_name.empty() ||
        material_preset_names_in.empty() ||
        printer_preset_name.empty())
        return "Request is not completed. All of Print/Material/Printer profiles have to be entered";

    // check preset bundle

    PresetBundle preset_bundle;
    if (!load_preset_bundle_from_datadir(preset_bundle))
        return Slic3r::format("Failed to load data from the datadir '%1%'.", data_dir());

    // check existance of required profiles

    std::string errors;

    const Preset* printer_preset = preset_bundle.printers.find_preset(printer_preset_name);
    if (!printer_preset)
        errors += "\n" + Slic3r::format("Printer profile '%1%' wasn't found.", printer_preset_name);
    else if (printer_technology == ptUnknown)
        printer_technology = printer_preset->printer_technology();
    else if (printer_technology != printer_preset->printer_technology())
        errors += "\n" + std::string("Printer technology of the selected printer preset is differs with required printer technology");

    PresetCollection& print_presets = printer_technology == ptFFF ? preset_bundle.prints    : preset_bundle.sla_prints;

    const Preset* print_preset = print_presets.find_preset(print_preset_name);
    if (!print_preset)
        errors += "\n" + Slic3r::format("Print profile '%1%' wasn't found.", print_preset_name);

    PresetCollection& material_presets = printer_technology == ptFFF ? preset_bundle.filaments : preset_bundle.sla_materials;

    auto check_material = [&material_presets] (const std::string& name, std::string& errors) -> void {
        const Preset* material_preset = material_presets.find_preset(name);
        if (!material_preset)
            errors += "\n" + Slic3r::format("Material profile '%1%' wasn't found.", name);
    };

    check_material(material_preset_names_in.front(), errors);
    if (material_preset_names_in.size() > 1) {
        for (size_t idx = 1; idx < material_preset_names_in.size(); idx++) {
            if (material_preset_names_in[idx] != material_preset_names_in.front())
                check_material(material_preset_names_in[idx], errors);
        }
    }

    if (!errors.empty())
        return errors;

    // check and update list of material presets

    std::vector<std::string> material_preset_names = material_preset_names_in;

    if (printer_technology == ptSLA && material_preset_names.size() > 1) {
        BOOST_LOG_TRIVIAL(warning) << "Note: More than one sla material profiles were entered. Extras material profiles will be ignored.";
        material_preset_names.resize(1);
    }

    if (printer_technology == ptFFF) {
        const int extruders_count = int(static_cast<const ConfigOptionFloats*>(printer_preset->config.option("nozzle_diameter"))->values.size());
        if (extruders_count > int(material_preset_names.size())) {
            BOOST_LOG_TRIVIAL(warning) << "Note: Less than needed filament profiles were entered. Missed filament profiles will be filled with first material.";
            material_preset_names.reserve(extruders_count);
            for (int i = extruders_count - material_preset_names.size(); i > 0; i--)
                material_preset_names.push_back(material_preset_names.front());
        }
        else if (extruders_count < int(material_preset_names.size())) {
            BOOST_LOG_TRIVIAL(warning) << "Note: More than needed filament profiles were entered. Extras filament profiles will be ignored.";
            material_preset_names.resize(extruders_count);
        }
    }

    // check profiles compatibility

    const PresetWithVendorProfile printer_preset_with_vendor_profile = preset_bundle.printers.get_preset_with_vendor_profile(*printer_preset);
    const PresetWithVendorProfile print_preset_with_vendor_profile   = print_presets.get_preset_with_vendor_profile(*print_preset);

    if (!is_compatible_with_printer(print_preset_with_vendor_profile, printer_preset_with_vendor_profile))
        errors += "\n" + Slic3r::format("Print profile '%1%' is not compatible with printer profile %2%.", print_preset_name, printer_preset_name);

    auto check_material_preset_compatibility = [&material_presets, printer_preset_name, print_preset_name, printer_preset_with_vendor_profile, print_preset_with_vendor_profile]
                                         (const std::string& name, std::string& errors) -> void {
        const Preset* material_preset = material_presets.find_preset(name);
        const PresetWithVendorProfile material_preset_with_vendor_profile = material_presets.get_preset_with_vendor_profile(*material_preset);

        if (!is_compatible_with_printer(material_preset_with_vendor_profile, printer_preset_with_vendor_profile))
            errors += "\n" + Slic3r::format("Material profile '%1%' is not compatible with printer profile %2%.", name, printer_preset_name);

        if (!is_compatible_with_print(material_preset_with_vendor_profile, print_preset_with_vendor_profile, printer_preset_with_vendor_profile))
            errors += "\n" + Slic3r::format("Material profile '%1%' is not compatible with print profile %2%.", name, print_preset_name);
    };

    check_material_preset_compatibility(material_preset_names.front(), errors);
    if (material_preset_names.size() > 1) {
        for (size_t idx = 1; idx < material_preset_names.size(); idx++) {
            if (material_preset_names[idx] != material_preset_names.front())
                check_material_preset_compatibility(material_preset_names[idx], errors);
        }
    }

    if (!errors.empty())
        return errors;

    // get full print configuration

    preset_bundle.printers.select_preset_by_name(printer_preset_name, true);
    print_presets.select_preset_by_name(print_preset_name, true);
    if (printer_technology == ptSLA)
        material_presets.select_preset_by_name(material_preset_names.front(), true);
    else if (printer_technology == ptFFF) {
        auto& extruders_filaments = preset_bundle.extruders_filaments;
        extruders_filaments.clear();
        for (size_t i = 0; i < material_preset_names.size(); ++i)
            extruders_filaments.emplace_back(ExtruderFilaments(&preset_bundle.filaments, i, material_preset_names[i]));
        if (extruders_filaments.size() == 1)
            preset_bundle.filaments.select_preset_by_name(material_preset_names[0], false);
    }

    config = preset_bundle.full_config();

    return "";
}

} // namespace Slic3r
