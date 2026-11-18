#include "Slic3r/Biz/Preset/PrinterPresetSelector.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

#include "Slic3r/Log.hpp"
#include "nlohmann/json.hpp"
#include "fmt/format.h"

#include <algorithm>
#include <charconv>

namespace Slic3r::Biz::Preset {

PrinterPresetSelector::PrinterPresetSelector(PresetInteractor& preset_interactor) :
    m_preset_interactor(preset_interactor)
{}

void PrinterPresetSelector::warn(const std::string& msg)
{
    this->invoke_listeners<IPrinterPresetSelectorListener>([&msg](auto* listener) {
        listener->on_printer_preset_selection_warning(msg);
    });
}

PreferredMaterialTypes PrinterPresetSelector::connect_material_types(
    const nlohmann::json& printer_json,
    size_t slot_count
)
{
    PreferredMaterialTypes material_types(slot_count);
    if (!printer_json.contains("tools") || !printer_json["tools"].is_object()) {
        return material_types;
    }

    for (const auto& [tool_key, tool_json] : printer_json["tools"].items()) {
        size_t slot_idx = 0;
        const auto [ptr, ec] =
            std::from_chars(tool_key.data(), tool_key.data() + tool_key.size(), slot_idx);
        if (ec != std::errc{} || ptr != tool_key.data() + tool_key.size()
            || slot_idx >= slot_count)
        {
            continue;
        }

        if (tool_json.contains("material_package_instance") && tool_json["material_package_instance"].is_object()) {
            const auto& mpi = tool_json["material_package_instance"];
            if (mpi.contains("package") && mpi["package"].is_object()) {
                const auto& pkg = mpi["package"];
                if (pkg.contains("material") && pkg["material"].is_object()) {
                    const auto& mat = pkg["material"];
                    if (mat.contains("type") && mat["type"].is_string()) {
                        material_types[slot_idx] = mat["type"].get<std::string>();
                    }
                }
            }
        }
    }
    return material_types;
}

void PrinterPresetSelector::warn_material_mismatches(const PreferredMaterialTypes& material_types)
{
    const auto& materials = m_preset_interactor.selected_printer_preset().materials;

    std::vector<std::string> mismatches;
    for (size_t slot_idx = 0, n = std::min(material_types.size(), materials.size());
         slot_idx < n;
         ++slot_idx)
    {
        const std::string& target_type = material_types[slot_idx];
        if (target_type.empty()) {
            continue;
        }
        const auto it = materials[slot_idx].config_box().find("filament_type");
        if (it.item && it.item->template get<std::string>() == target_type) {
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
