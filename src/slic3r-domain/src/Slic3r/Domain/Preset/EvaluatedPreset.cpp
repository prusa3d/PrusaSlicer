#include "Slic3r/Domain/Preset/EvaluatedPreset.hpp"

namespace Slic3r::Domain::Preset {

const EvaluatedToolPrintPreset* EvaluatedPrintPreset::find_tool_preset_by_id(
    size_t tool_idx,
    const std::string& id
) const
{
    const auto& tool_presets = tools[tool_idx];
    auto it = std::ranges::find_if(tool_presets, [&id](const EvaluatedToolPrintPreset& p) {
        return p.preset.id == id;
    });
    return it == tool_presets.end() ? nullptr : &*it;
}

const EvaluatedMaterialPreset* EvaluatedPrintPreset::find_material_preset_by_id(
    size_t tool_idx,
    const std::string& id
) const
{
    const auto& tool_materials = materials[tool_idx];
    auto it = std::ranges::find_if(tool_materials, [&id](const EvaluatedMaterialPreset& p) {
        return p.preset.id == id;
    });
    return it == tool_materials.end() ? nullptr : &*it;
}

const EvaluatedPrintPreset* EvaluatedPrinterPreset::find_print_preset_by_id(const std::string& id) const
{
    auto it = std::ranges::find_if(prints, [&id](const EvaluatedPrintPreset& p) {
        return p.preset.id == id;
    });
    return it == prints.end() ? nullptr : &*it;
}

bool EvaluatedPrinterPreset::is_valid() const
{
    return !prints.empty() && std::ranges::all_of(prints, [](const EvaluatedPrintPreset& p) {
        return !p.tools.empty();
    });
}

} // namespace Slic3r::Domain::Preset
