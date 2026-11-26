#include "Slic3r/Biz/Preset/PrinterPresetSelector.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "Slic3r/Log.hpp"
#include "nlohmann/json.hpp"
#include "fmt/format.h"

#include <algorithm>

namespace Slic3r::Biz::Preset {

namespace {

// Connect reports the color as "#rrggbbaa", filament colors are "#rrggbb".
std::string material_color(const nlohmann::json& material)
{
    if (!material.contains("primary_color") || !material["primary_color"].is_object()) {
        return {};
    }
    const auto& primary_color = material["primary_color"];
    if (!primary_color.contains("color_rgba") || !primary_color["color_rgba"].is_string()) {
        return {};
    }
    return Algorithms::Color::strip_alpha(primary_color["color_rgba"].get<std::string>())
        .value_or(std::string{});
}

bool has_material_type(
    const Domain::Preset::EvaluatedMaterialPreset::Preset& preset,
    const std::string& type
)
{
    const auto it = preset.config_box().find("filament_type");
    return it.item && it.item->template get<std::string>() == type;
}

// The material Connect reports in a tool, nullptr when the tool is not reported or is empty.
const nlohmann::json*
reported_material(const nlohmann::json& tools_json, const std::string& tool_key)
{
    const auto tool = tools_json.find(tool_key);
    if (tool == tools_json.end()) {
        return nullptr;
    }
    const nlohmann::json* node = &*tool;
    for (const char* key : {"material_package_instance", "package", "material"}) {
        if (!node->is_object() || !node->contains(key)) {
            return nullptr;
        }
        node = &(*node)[key];
    }
    return node->is_object() ? node : nullptr;
}

} // namespace

std::string connect_tool_key(const Domain::Preset::HwPrinterConfig& hw_config, size_t slot_index)
{
    return Domain::Preset::to_string(
        Domain::Preset::MaterialIterator::from_slot_index(hw_config, slot_index).address()
    );
}

PrinterPresetSelector::PrinterPresetSelector(PresetInteractor& preset_interactor) :
    m_preset_interactor(preset_interactor)
{}

void PrinterPresetSelector::warn(const std::string& msg)
{
    this->invoke_listeners<IPrinterPresetSelectorListener>([&msg](auto* listener) {
        listener->on_printer_preset_selection_warning(msg);
    });
}

std::vector<ConnectMaterial> PrinterPresetSelector::connect_materials(
    const nlohmann::json& printer_json,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    std::vector<ConnectMaterial> materials(hw_config.material_slot_count());
    if (!printer_json.contains("tools") || !printer_json["tools"].is_object()) {
        return materials;
    }

    const auto& tools_json = printer_json["tools"];
    for (size_t slot_idx = 0; slot_idx < materials.size(); ++slot_idx) {
        // A slot of a feeder (MMU) is reported under its address, e.g. "0.2".
        const nlohmann::json* mat =
            reported_material(tools_json, connect_tool_key(hw_config, slot_idx));
        if (mat == nullptr) {
            continue;
        }

        ConnectMaterial& material = materials[slot_idx];
        if (mat->contains("type") && (*mat)["type"].is_string()) {
            material.type = (*mat)["type"].get<std::string>();
        } else if (mat->contains("abbreviation") && (*mat)["abbreviation"].is_string()) {
            // The type is optional in the material database, the abbreviation is not.
            material.type = (*mat)["abbreviation"].get<std::string>();
        }
        if (mat->contains("uuid") && (*mat)["uuid"].is_string()) {
            material.matdb_uuid = (*mat)["uuid"].get<std::string>();
        }
        material.color = material_color(*mat);
    }
    return materials;
}

PreferredMaterialTypes
PrinterPresetSelector::material_types(const std::vector<ConnectMaterial>& materials)
{
    PreferredMaterialTypes material_types;
    material_types.reserve(materials.size());
    for (const ConnectMaterial& material : materials) {
        material_types.push_back({material.type, material.matdb_uuid, material.matdb_material});
    }
    return material_types;
}

std::vector<ConnectSlotColor> PrinterPresetSelector::complete_connect_materials(
    const std::vector<ConnectMaterial>& materials,
    const std::vector<std::string>& previous_matdb_uuids,
    const MatDbMaterialMatcher& matches_matdb_material
)
{
    const auto& selected_preset = m_preset_interactor.selected_printer_preset();
    const size_t slot_count     = std::min(materials.size(), selected_preset.materials.size());
    std::vector<ConnectSlotColor> colors(slot_count);
    // Types of the filaments that do not hold their MDB material, to warn about those that do
    // not hold a preset of their type either.
    PreferredMaterialTypes unmatched_types(slot_count);

    for (size_t slot_idx = 0; slot_idx < slot_count; ++slot_idx) {
        const ConnectMaterial& material = materials[slot_idx];
        const std::string previous_matdb_uuid =
            slot_idx < previous_matdb_uuids.size() ? previous_matdb_uuids[slot_idx] : std::string{};
        const std::string matdb_uuid{selected_preset.material_matdb_uuid(slot_idx)};

        // The color follows the material: it is taken only once the slot holds the reported
        // filament, and a slot that got or lost an MDB material must not keep the color of
        // the previous one.
        const auto set_color =
            [&colors, &selected_preset, &previous_matdb_uuid, &material, slot_idx]
        {
            ConnectSlotColor& color = colors[slot_idx];
            color.color             = material.color;
            color.reset_to_auto     = color.color.empty()
                && selected_preset.material_matdb_uuid(slot_idx) != previous_matdb_uuid;
        };

        // The printer switch selected the preset the material selection offers first for
        // a material from the material database (MDB), together with its uuid, which brings
        // the brand and name of the material along.
        if (material.matdb_material.has_value()
            && matches_matdb_material
            && matches_matdb_material(slot_idx, *material.matdb_material))
        {
            // The switch kept another preset offered for the material.
            if (matdb_uuid != material.matdb_material->uuid) {
                m_preset_interactor
                    .set_material_matdb_uuid(slot_idx, material.matdb_material->uuid);
            }
            set_color();
            continue;
        }

        if (material.type.empty()) {
            continue;
        }
        if (!has_material_type(selected_preset.materials[slot_idx], material.type)) {
            unmatched_types[slot_idx].type = material.type;
            continue;
        }
        // The switch stored the reported uuid with the preset of the type, replacing the MDB
        // material of the previous filament, unless it kept a preset of that type.
        if (matdb_uuid != material.matdb_uuid) {
            m_preset_interactor.set_material_matdb_uuid(slot_idx, material.matdb_uuid);
        }
        set_color();
    }

    warn_material_mismatches(unmatched_types);
    return colors;
}

void PrinterPresetSelector::warn_material_mismatches(const PreferredMaterialTypes& material_types)
{
    const auto& materials = m_preset_interactor.selected_printer_preset().materials;

    std::vector<std::string> mismatches;
    for (size_t slot_idx = 0, n = std::min(material_types.size(), materials.size());
         slot_idx < n;
         ++slot_idx)
    {
        const std::string& target_type = material_types[slot_idx].type;
        if (target_type.empty() || has_material_type(materials[slot_idx], target_type)) {
            continue;
        }
        SPDLOG_WARN("Connect material sync: No matching material preset found for slot {} (Target type: {})", slot_idx, target_type);
        // TRN Warning line, first {} is the material type, second {} the slot number.
        mismatches.push_back(fmt::format(fmt::runtime(_u8L("No material profile matches {} in slot {}.")), target_type, slot_idx + 1));
    }
    warn_mismatches(mismatches);
}

void PrinterPresetSelector::warn_mismatches(const std::vector<std::string>& mismatches)
{
    if (mismatches.empty()) {
        return;
    }
    std::string msg;
    for (const std::string& mismatch : mismatches) {
        if (!msg.empty()) {
            msg += '\n';
        }
        msg += mismatch;
    }
    warn(msg);
}

} // namespace Slic3r::Biz::Preset
