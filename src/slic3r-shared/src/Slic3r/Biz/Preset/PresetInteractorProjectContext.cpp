#include "Slic3r/Biz/Preset/PresetInteractorProjectContext.hpp"

namespace {

template <typename T>
concept StructWithId = requires (T a){
    a.id;

    requires std::same_as<std::string, std::remove_cvref_t<decltype(a.id)>>;
};

template <StructWithId T, typename K>
const T* find_by_id(const std::map<K, std::vector<T>>& container, const K& parent_id, const std::string& id)
{
    const auto candidates_it = container.find(parent_id);
    if (candidates_it == container.end())
        return nullptr;
    const auto& candidates = candidates_it->second;
    auto it = std::find_if(candidates.begin(), candidates.end(), [&id](const auto& x){ return x.id == id; });
    return it == candidates.end() ? nullptr : &*it;
}

template <StructWithId T, typename K>
const T* find_by_id(const std::map<K, std::vector<std::vector<T>>>& container, const K& parent_id, size_t slot_index, const std::string& id)
{
    const auto slots_it = container.find(parent_id);
    if (slots_it == container.end())
        return nullptr;
    const auto& slots =  slots_it->second;
    const auto& candidates = slots.at(slot_index);
    auto it = std::find_if(candidates.begin(), candidates.end(), [&id](const auto& x){ return x.id == id; });
    return it == candidates.end() ? nullptr : &*it;
}

}

namespace Slic3r::Biz::Preset {

using PrinterPreset = RuntimePresets::PrinterPreset;
using PrintPreset = RuntimePresets::PrintPreset;
using ToolPrintPreset = RuntimePresets::ToolPrintPreset;
using MaterialPreset = RuntimePresets::MaterialPreset;

const Domain::Preset::HwPrinterConfig* RuntimePresets::find_printer_config_by_id(const std::string& hw_config_id) const
{
    auto it = std::find_if(
        printer_configs.begin(),
        printer_configs.end(),
        [&hw_config_id](const auto& x) { return x.second.id == hw_config_id; }
    );
    return it == printer_configs.end() ? nullptr : &it->second;
}

Domain::Preset::HwPrinterConfig* RuntimePresets::find_printer_config_by_id(const std::string& hw_config_id)
{
    auto it = std::find_if(
        printer_configs.begin(),
        printer_configs.end(),
        [&hw_config_id](const auto& x) { return x.second.id == hw_config_id; }
    );
    return it == printer_configs.end() ? nullptr : &it->second;
}

const PrinterPreset* RuntimePresets::find_printer_preset_by_id(const std::string& hw_config_id, const std::string& printer_preset_id) const
{
    return find_by_id(printer, hw_config_id, printer_preset_id);
}

const PrintPreset* RuntimePresets::find_print_preset_by_id(const HwConfigPrinterKey& parent, const std::string& print_id) const
{
    return find_by_id(print, parent, print_id);
}

const ToolPrintPreset* RuntimePresets::find_tool_print_preset_by_id(const HwConfingPrinterPrintKey& parent, size_t tool_index, const std::string& tool_print_id) const
{
    return find_by_id(tool_print, parent, tool_index, tool_print_id);
}

const MaterialPreset* RuntimePresets::find_material_preset_by_id(const HwConfingPrinterPrintKey& parent, size_t material_index, const std::string& material_id) const
{
    return find_by_id(material, parent, material_index, material_id);
}


void RuntimePresets::add_tool_print(const HwConfingPrinterPrintKey& parent, const Domain::Preset::HwPrinterConfig& hw_config, size_t tool_index, const ToolPrintPreset& tpp)
{
    auto& tools = tool_print[parent];
    if (tools.empty())
        tools.resize(hw_config.tool_count);
    tools[tool_index].push_back(tpp);
}

void RuntimePresets::add_material(const HwConfingPrinterPrintKey& parent, const Domain::Preset::HwPrinterConfig& hw_config, size_t slot_index, const MaterialPreset& m)
{
    auto& materials = material[parent];
    if (materials.empty())
        materials.resize(hw_config.material_slot_count());
    materials[slot_index].push_back(m);
}


}