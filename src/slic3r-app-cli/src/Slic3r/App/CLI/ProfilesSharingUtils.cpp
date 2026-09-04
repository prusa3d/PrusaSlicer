#include "Slic3r/App/CLI/ProfilesSharingUtils.hpp"

#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/LegacyFormat.hpp"

#include <cassert>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <ranges>
#include <string>
#include <vector>

#include "libslic3r/BuildVolume.hpp"

using Slic3r::Domain::ConfigBox;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec2ds;
using Slic3r::Domain::Preset::Bundle;
using Slic3r::Domain::Preset::EvaluatedMaterialPreset;
using Slic3r::Domain::Preset::EvaluatedPrinterPreset;
using Slic3r::Domain::Preset::EvaluatedPrinterPresets;
using Slic3r::Domain::Preset::EvaluatedPrintPreset;
using Slic3r::Domain::Preset::EvaluatedToolPrintPreset;
using Slic3r::Domain::Preset::SingleToolEvaluatedMaterialPresets;
using Slic3r::Domain::Preset::SingleToolEvaluatedToolPrintPresets;
using Slic3r::Domain::Preset::VendorBundle;

namespace Slic3r::App::CLI {

static void add_profile_node(
    nlohmann::ordered_json& printer_profiles_node,
    const EvaluatedPrinterPreset& evaluated_preset
)
{
    nlohmann::ordered_json profile_node = nlohmann::ordered_json::object();

    const ConfigBox& config    = evaluated_preset.preset.config_box();
    const size_t extruders_cnt = evaluated_preset.technology() == PrinterTechnology::SLA ?
        0 :
        evaluated_preset.hw_config.tool_count;

    profile_node["name"] = evaluated_preset.hw_config.name;
    if (extruders_cnt > 0) {
        profile_node["extruders_cnt"] = extruders_cnt;
    }

    const double max_print_height = config.items.opt("max_print_height").get<double>();
    const Vec2ds& bed_shape       = config.items.opt("bed_shape").get<Vec2ds>();

    BuildVolume build_volume = BuildVolume{bed_shape, max_print_height};
    BoundingBoxf bb          = build_volume.bounding_volume2d();

    Vec2d origin_pt;
    if (build_volume.type() == BuildVolume::Type::Circle) {
        origin_pt = build_volume.bed_center();
    } else {
        origin_pt = to_2d(-1 * build_volume.bounding_volume().min);
    }

    const std::string origin = Slic3r::format(
        "[%1%, %2%]",
        Domain::fuzzy_compare(origin_pt.x(), 0.) ? 0 : origin_pt.x(),
        Domain::fuzzy_compare(origin_pt.y(), 0.) ? 0 : origin_pt.y()
    );

    nlohmann::ordered_json bed_node = nlohmann::ordered_json::object();
    bed_node["type"]                = build_volume.type_name();
    bed_node["width"]               = bb.max.x() - bb.min.x();
    bed_node["height"]              = bb.max.y() - bb.min.y();
    bed_node["origin"]              = origin;
    bed_node["max_print_height"]    = max_print_height;

    profile_node["bed"] = std::move(bed_node);

    printer_profiles_node.push_back(std::move(profile_node));
}

static void add_printer_models(
    nlohmann::ordered_json& printer_models_node,
    const VendorBundle& vendor_bundle,
    const EvaluatedPrinterPresets& evaluated_presets
)
{
    std::map<std::string, std::vector<std::reference_wrapper<const EvaluatedPrinterPreset>>>
        grouped_printer_presets;
    for (const std::vector<EvaluatedPrinterPreset>& evaluated_presets_items :
         evaluated_presets | std::views::values)
    {
        for (const EvaluatedPrinterPreset& evaluated_preset : evaluated_presets_items) {
            if (evaluated_preset.hw_config.vendor_id != vendor_bundle.vendor_data.info.id) {
                continue;
            }

            grouped_printer_presets[evaluated_preset.preset.id].emplace_back(
                std::cref(evaluated_preset)
            );
        }
    }

    for (const auto& [preset_id, printer_presets] : grouped_printer_presets) {
        const EvaluatedPrinterPreset& first_printer_presets = printer_presets.front().get();

        nlohmann::ordered_json variants_node              = nlohmann::ordered_json::array();
        nlohmann::ordered_json printer_profiles_node      = nlohmann::ordered_json::array();
        nlohmann::ordered_json user_printer_profiles_node = nlohmann::ordered_json::array();

        if (first_printer_presets.technology() == PrinterTechnology::SLA) {
            add_profile_node(printer_profiles_node, first_printer_presets);
            if (printer_profiles_node.empty() && user_printer_profiles_node.empty()) {
                continue;
            }
        } else {
            assert(first_printer_presets.technology() == PrinterTechnology::FFF);

            for (const std::reference_wrapper<const EvaluatedPrinterPreset>& printer_preset_ref :
                 printer_presets)
            {
                const EvaluatedPrinterPreset& printer_preset = printer_preset_ref.get();

                printer_profiles_node      = nlohmann::ordered_json::array();
                user_printer_profiles_node = nlohmann::ordered_json::array();

                add_profile_node(printer_profiles_node, printer_preset);
                if (printer_profiles_node.empty() && user_printer_profiles_node.empty()) {
                    continue;
                }

                nlohmann::ordered_json variant_node = nlohmann::ordered_json::object();
                variant_node["id"]                  = printer_preset.hw_config.id;
                variant_node["name"]                = printer_preset.hw_config.name;
                variant_node["printer_profiles"]    = printer_profiles_node;
                if (!user_printer_profiles_node.empty()) {
                    variant_node["user_printer_profiles"] = user_printer_profiles_node;
                }

                variants_node.push_back(std::move(variant_node));
            }

            if (variants_node.empty()) {
                continue;
            }
        }

        nlohmann::ordered_json data_node = nlohmann::ordered_json::object();
        data_node["id"]                  = first_printer_presets.preset.id;
        data_node["name"]                = first_printer_presets.preset.name;
        data_node["technology"] =
            first_printer_presets.technology() == PrinterTechnology::FFF ? "FFF" : "SLA";

        if (!variants_node.empty()) {
            data_node["variants"] = std::move(variants_node);
        } else {
            data_node["printer_profiles"] = printer_profiles_node;
            if (!user_printer_profiles_node.empty()) {
                data_node["user_printer_profiles"] = user_printer_profiles_node;
            }
        }

        data_node["vendor_name"] = vendor_bundle.vendor_data.info.name;
        data_node["vendor_id"]   = vendor_bundle.vendor_data.info.id;

        printer_models_node.push_back(std::move(data_node));
    }
}

std::string get_json_printer_models(const Bundle& preset_bundle)
{
    nlohmann::ordered_json printer_models_node = nlohmann::ordered_json::array();
    for (const VendorBundle& vendor_bundle : preset_bundle.vendor_bundles | std::views::values) {
        add_printer_models(printer_models_node, vendor_bundle, preset_bundle.evaluated_presets);
    }

    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    root["printer_models"]      = std::move(printer_models_node);

    return root.dump(4);
}

static std::string get_installed_print_tool_filament_profiles(
    const EvaluatedPrinterPreset& printer_preset
)
{
    const PrinterTechnology printer_technology = printer_preset.technology();
    const std::string material_node_name       = (printer_technology == PrinterTechnology::FFF) ?
              "filament_profiles" :
              "sla_material_profiles";

    nlohmann::ordered_json print_profiles = nlohmann::ordered_json::array();

    for (const EvaluatedPrintPreset& print_preset : printer_preset.prints) {
        nlohmann::ordered_json materials_profile_node = nlohmann::ordered_json::array();
        for (const SingleToolEvaluatedMaterialPresets& material_presets : print_preset.materials) {
            for (const EvaluatedMaterialPreset& material_preset : material_presets) {
                materials_profile_node.push_back(material_preset.preset.name);
            }
        }

        nlohmann::ordered_json tool_prints_profile_node = nlohmann::ordered_json::array();
        for (const SingleToolEvaluatedToolPrintPresets& tool_presets : print_preset.tools) {
            for (const EvaluatedToolPrintPreset& tool_preset : tool_presets) {
                tool_prints_profile_node.push_back(tool_preset.preset.name);
            }
        }

        nlohmann::ordered_json print_profile_node = nlohmann::ordered_json::object();
        print_profile_node["name"]                = print_preset.preset.name;

        if (printer_technology == PrinterTechnology::FFF) {
            print_profile_node["tool_print_profiles"] = std::move(tool_prints_profile_node);
        }

        print_profile_node[material_node_name] = std::move(materials_profile_node);

        print_profiles.push_back(std::move(print_profile_node));
    }

    if (print_profiles.empty()) {
        return "";
    }

    nlohmann::ordered_json tree = nlohmann::ordered_json::object();
    tree["printer_profile"]     = printer_preset.preset.name;
    tree["print_profiles"]      = std::move(print_profiles);

    return tree.dump(4);
}

std::string get_json_print_tool_filament_profiles(
    const Bundle& preset_bundle,
    const std::string& printer_profile
)
{
    // Find a printer preset by name in evaluated presets.
    const EvaluatedPrinterPreset* printer_preset = nullptr;
    for (const std::vector<EvaluatedPrinterPreset>& evaluated_presets :
         preset_bundle.evaluated_presets | std::views::values)
    {
        const auto printer_preset_it = std::ranges::find_if(
            evaluated_presets,
            [&printer_profile](const EvaluatedPrinterPreset& preset)
            { return preset.hw_config.name == printer_profile; }
        );
        if (printer_preset_it != evaluated_presets.end()) {
            printer_preset = &*printer_preset_it;
            break;
        }
    }

    if (printer_preset != nullptr) {
        return get_installed_print_tool_filament_profiles(*printer_preset);
    }

    return "";
}

} // namespace Slic3r::App::CLI
