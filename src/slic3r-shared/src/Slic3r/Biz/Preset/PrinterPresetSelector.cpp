#include "Slic3r/Biz/Preset/PrinterPresetSelector.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/HwConfigMatchers.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "Slic3r/Log.hpp"
#include "nlohmann/json.hpp"
#include "fmt/format.h"

#include <cmath>

namespace Slic3r::Biz::Preset {

namespace {

bool
compare_feature(const Domain::Preset::FeatureValue& hw_feature, const nlohmann::json& json_feature)
{
    return std::visit(
        [&json_feature](const auto& val) -> bool
        {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, double>) {
                if (!json_feature.is_number()) {
                    return false;
                }
                constexpr double epsilon = 1e-6;
                return std::abs(json_feature.get<double>() - val) <= epsilon;
            } else if constexpr (std::is_same_v<T, bool>) {
                return json_feature.is_boolean() && json_feature.get<bool>() == val;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return json_feature.is_string() && json_feature.get<std::string>() == val;
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return json_feature.is_null();
            } else if constexpr (std::is_same_v<T, Domain::JsonArray>) {
                if (!json_feature.is_array() || json_feature.size() != val.size()) {
                    return false;
                }
                for (size_t i = 0; i < val.size(); ++i) {
                    if (!compare_feature(val[i], json_feature[i])) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, Domain::JsonObject>) {
                if (!json_feature.is_object() || json_feature.size() != val.size()) {
                    return false;
                }
                for (const auto& [key, inner_val] : val) {
                    if (!json_feature.contains(key)
                        || !compare_feature(inner_val, json_feature[key])) {
                        return false;
                    }
                }
                return true;
            }

            return false;
        },
        static_cast<const Domain::JsonVariant&>(hw_feature)
    );
}

const PresetItem* find_matching_preset_item(
    const auto& printer_presets,
    const Domain::Preset::HwPrinterConfig& hw_config
)
{
    for (size_t i = 0; i < printer_presets.size(); i++) {
        const PresetItem& item = printer_presets.at(i);

        if (item.hw_printer_config_id == hw_config.id
            && item.origin == Domain::Preset::PresetOrigin::System)
        {
            return &item;
        }
    }

    return nullptr;
}
} // namespace

PrinterPresetSelector::PrinterPresetSelector(PresetInteractor& preset_interactor) :
    m_preset_interactor(preset_interactor)
{}

void PrinterPresetSelector::warn(const std::string& msg)
{
    this->invoke_listeners<IPrinterPresetSelectorListener>([&msg](auto* listener) {
        listener->on_printer_preset_selection_warning(msg);
    });
}

bool PrinterPresetSelector::select_for_hw_config(const Domain::Preset::HwPrinterConfig& hw_config)
{
    if (m_preset_interactor.selected_printer_preset().hw_config.id == hw_config.id) {
        return true;
    }

    const PresetItem* item =
        find_matching_preset_item(m_preset_interactor.printer_presets().items(), hw_config);

    if (!item) {
        SPDLOG_INFO("No system printer preset for hw config {}.", hw_config.id);
        // TRN Warning shown when a picked printer has no matching printer profile.
        warn(_u8L("No system printer profile matches the selected printer."));
        return false;
    }

    m_preset_interactor.select_printer_preset(hw_config.id, item->id, true);
    return true;
}

bool PrinterPresetSelector::select_for_connect_printer(const nlohmann::json& printer_json)
{
    const Domain::Preset::HwPrinterConfig* config =
        find_matching_printer_config(m_preset_interactor.get_printer_configs(), printer_json);

    if (!config) {
        SPDLOG_INFO("No matching hw config for Connect printer.");
        // TRN Warning shown when a picked Connect printer has no matching printer profile.
        warn(_u8L("No local printer profile matches the selected printer."));
        return false;
    }

    if (!select_for_hw_config(*config)) {
        return false;
    }

    std::vector<std::string> mismatches;
    select_tools(printer_json, mismatches);
    select_materials(printer_json, mismatches);

    if (!mismatches.empty()) {
        std::string msg;
        for (const std::string& mismatch : mismatches) {
            if (!msg.empty()) {
                msg += '\n';
            }
            msg += mismatch;
        }
        warn(msg);
    }
    return true;
}

void PrinterPresetSelector::select_tools(
    const nlohmann::json& printer_json,
    std::vector<std::string>& mismatches
)
{
    if (!printer_json.contains("tools") || !printer_json["tools"].is_object()) {
        return;
    }

    const Domain::Preset::FeatureDefs* vendor_tool_features =
        m_preset_interactor.get_vendor_tool_feature_defs();

    for (const auto& [tool_key, tool_json] : printer_json["tools"].items()) {
        const auto address = Domain::Preset::to_address(tool_key);

        if (!address) {
            SPDLOG_WARN(
                "Connect tool sync: Address could not be parsed from '{}': {}",
                tool_key,
                address.error()
            );
            continue;
        }

        if (address->size() != 1) {
            continue;
        }

        const size_t tool_idx = address->front();

        if (!tool_json.contains("features") || tool_idx >= m_preset_interactor.tool_items().size()) {
            continue;
        }

        const auto& tool_list       = m_preset_interactor.tool_items().at(tool_idx);
        const auto& available_tools = tool_list.items();

        std::string matching_tool_id;

        for (size_t i = 0; i < available_tools.size(); ++i) {
            const Domain::Preset::HwToolConfigDef& tool_def = available_tools.at(i);
            bool matches                                    = true;

            for (const auto& [feature_key, json_feature_val] : tool_json["features"].items()) {
                const Domain::Preset::FeatureValue* expected_value = nullptr;

                if (auto it = tool_def.features.find(feature_key); it != tool_def.features.end()) {
                    expected_value = &it->second.default_value;
                } else if (vendor_tool_features) {
                    if (auto vendor_it = vendor_tool_features->find(feature_key);
                        vendor_it != vendor_tool_features->end())
                    {
                        expected_value = &vendor_it->second.default_value;
                    }
                }

                if (!expected_value) {
                    continue;
                }

                if (!compare_feature(*expected_value, json_feature_val)) {
                    matches = false;
                    break;
                }
            }

            if (matches) {
                matching_tool_id = tool_def.id;
                break;
            }
        }

        if (!matching_tool_id.empty()) {
            m_preset_interactor.select_printer_tool_item(tool_idx, matching_tool_id);
        } else {
            SPDLOG_WARN("Connect tool sync: No matching tool definition found for tool index {}", tool_idx);
            // TRN Warning line, {} is the tool number.
            mismatches.push_back(fmt::format(fmt::runtime(_u8L("No tool profile matches tool {}.")), tool_idx + 1));
        }
    }
}

void PrinterPresetSelector::select_materials(
    const nlohmann::json& printer_json,
    std::vector<std::string>& mismatches
)
{
    if (!printer_json.contains("tools") || !printer_json["tools"].is_object()) {
        return;
    }

    const auto& tools_json = printer_json["tools"];
    size_t max_slots = m_preset_interactor.material_presets().size();

    const auto& selected_preset = m_preset_interactor.selected_printer_preset();
    const std::string& hw_config_id = selected_preset.hw_config.id;
    const std::string& printer_preset_id = selected_preset.printer.id;
    const std::string& print_preset_id = selected_preset.print.id;

    for (size_t slot_idx = 0; slot_idx < max_slots; ++slot_idx) {
        std::string tool_key = std::to_string(slot_idx);

        if (!tools_json.contains(tool_key)) {
            continue;
        }

        const auto& tool_json = tools_json[tool_key];
        std::string target_type;

        if (tool_json.contains("material_package_instance") && tool_json["material_package_instance"].is_object()) {
            const auto& mpi = tool_json["material_package_instance"];
            if (mpi.contains("package") && mpi["package"].is_object()) {
                const auto& pkg = mpi["package"];
                if (pkg.contains("material") && pkg["material"].is_object()) {
                    const auto& mat = pkg["material"];
                    if (mat.contains("type") && mat["type"].is_string()) {
                        target_type = mat["type"].get<std::string>();
                    }
                }
            }
        }

        if (target_type.empty()) {
            continue;
        }

        const auto& material_list       = m_preset_interactor.material_presets().at(slot_idx);
        const auto& available_materials = material_list.items();
        std::string matching_material_id;

        std::string best_prusament_id;
        size_t best_prusament_len = std::string::npos;
        std::string best_generic_id;
        size_t best_generic_len = std::string::npos;
        std::string first_match_id;

        for (size_t i = 0; i < available_materials.size(); ++i) {
            const PresetItem& preset = available_materials.at(i);

            for (const auto& [mat_preset, is_runtime] : m_preset_interactor.get_material_presets(hw_config_id, printer_preset_id, print_preset_id, slot_idx)) {
                if (is_runtime) {
                    continue;
                }

                if (mat_preset.get().id != preset.id) {
                    continue;
                }

                auto it = mat_preset.get().config_box().find("filament_type");
                if (it.item && it.item->template get<std::string>() == target_type) {
                    if (first_match_id.empty()) {
                        first_match_id = preset.id;
                    }

                    if (preset.name.starts_with("Prusament")) {
                        if (preset.name.length() < best_prusament_len) {
                            best_prusament_len = preset.name.length();
                            best_prusament_id = preset.id;
                        }
                    }
                    else if (preset.name.starts_with("Generic")) {
                        if (preset.name.length() < best_generic_len) {
                            best_generic_len = preset.name.length();
                            best_generic_id = preset.id;
                        }
                    }
                }
            }
        }

        if (!best_prusament_id.empty()) {
            matching_material_id = best_prusament_id;
        } else if (!best_generic_id.empty()) {
            matching_material_id = best_generic_id;
        } else {
            matching_material_id = first_match_id;
        }

        if (!matching_material_id.empty()) {
            m_preset_interactor.select_material_preset(slot_idx, matching_material_id);
        } else {
            SPDLOG_WARN("Connect material sync: No matching material preset found for slot {} (Target type: {})", slot_idx, target_type);
            // TRN Warning line, first {} is the material type, second {} the slot number.
            mismatches.push_back(fmt::format(fmt::runtime(_u8L("No material profile matches {} in slot {}.")), target_type, slot_idx + 1));
        }
    }
}

} // namespace Slic3r::Biz::Preset
