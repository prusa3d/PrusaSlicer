#include "Slic3r/Domain/Preset/Bundle.hpp"

#include <ranges>

namespace Slic3r::Domain::Preset {

const HwPrinterConfig* VendorBundle::find_printer_config(const std::string& id) const
{
    auto it = std::ranges::
        find_if(printer_configs, [&id](const HwPrinterConfig& config) { return config.id == id; });
    if (it == printer_configs.end())
        return nullptr;
    return &(*it);
}

HwPrinterConfig* VendorBundle::find_printer_config(const std::string& id)
{
    auto it = std::ranges::
        find_if(printer_configs, [&id](const HwPrinterConfig& config) { return config.id == id; });
    if (it == printer_configs.end())
        return nullptr;
    return &(*it);
}


const EvaluatedPrinterPreset* Bundle::find_printer_preset(
    const std::string& printer_hw_config_id,
    const std::string& printer_preset_id
) const
{
    const auto& printer_presets = evaluated_presets.find(printer_hw_config_id);

    if (printer_presets == evaluated_presets.end())
        return nullptr;

    auto it = std::ranges::find_if(printer_presets->second, [&printer_preset_id](const EvaluatedPrinterPreset& p) {
        return p.preset.id == printer_preset_id;
    });
    if (it != printer_presets->second.end())
        return &*it;
    return nullptr;
}

const HwPrinterConfig* Bundle::find_config_with_same_values(const HwPrinterConfig& printer_config) const
{
    auto it = std::ranges::find_if(
        printer_configs,
        [&printer_config](const auto& pair) { return pair.second.has_same_values(printer_config); }
    );
    return it == printer_configs.end() ? nullptr : &(it->second);
}

const EvaluatedPrinterPreset* Bundle::find_printer_preset_with_same_values(
    const std::string& hw_config_id,
    const EvaluatedPrinterPreset::Preset& printer_preset
) const
{
    const auto& evaluated_printer_presets = evaluated_presets.at(hw_config_id);
    return find_preset_with_same_value(printer_preset, evaluated_printer_presets);
}

namespace {

template <typename T>
Bundle::UsedSlots get_preset_used_slots(
    const EvaluatedPrinterPresets& evaluated_presets,
    const std::string& hw_config_id,
    const std::string& printer_id,
    const std::string& print_id,
    const std::function<const std::vector<std::vector<T>>&(const EvaluatedPrintPreset&)>& get_preset
)
{
    Bundle::UsedSlots ret;

    const auto& printers = evaluated_presets.at(hw_config_id);
    auto printer_it = std::ranges::find_if(printers, [&](const auto& p) { return p.preset.id == printer_id; });
    ASSERT(printer_it != printers.end());
    const auto& printer = *printer_it;
    auto print_it = std::ranges::find_if(printer.prints, [&](const auto& p) { return p.preset.id == print_id; });
    ASSERT(print_it != printer.prints.end());
    const auto& print = *print_it;
    auto& all_tools_presets = get_preset(print);
    for (size_t i = 0, n = all_tools_presets.size(); i < n; ++i) {
        const auto& tool_presets = all_tools_presets.at(i);
        if (std::ranges::any_of(tool_presets, [&](const auto& p) { return p.preset.id == print_id; })) {
            ret.push_back(i);
        }
    }

    return ret;
}

const AllToolsEvaluatedToolPrintPresets& get_tool_prints(const EvaluatedPrintPreset& p)
{
    return p.tools;
}
const AllToolsEvaluatedMaterialPresets& get_materials(const EvaluatedPrintPreset& p)
{
    return p.materials;
}


} // namespace

Bundle::UsedSlots Bundle::get_tool_print_preset_used_slots(
    const std::string& hw_config_id,
    const std::string& printer_id,
    const std::string& print_id
) const
{
    return get_preset_used_slots<EvaluatedToolPrintPreset>(
        evaluated_presets,
        hw_config_id,
        printer_id,
        print_id,
        get_tool_prints
    );
}

Bundle::UsedSlots Bundle::get_material_preset_used_slots(
    const std::string& hw_config_id,
    const std::string& printer_id,
    const std::string& print_id
) const
{
    return get_preset_used_slots<EvaluatedMaterialPreset>(
        evaluated_presets,
        hw_config_id,
        printer_id,
        print_id,
        get_materials
    );
}

} // namespace Slic3r::Domain::Preset
