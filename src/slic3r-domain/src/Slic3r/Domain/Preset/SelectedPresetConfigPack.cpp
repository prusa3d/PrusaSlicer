#include "Slic3r/Domain/Preset/SelectedPresetConfigPack.hpp"

namespace Slic3r::Domain::Preset {

SelectedPresetConfigPack::SelectedPresetConfigPack(const SelectedPreset& selected_preset) :
    m_selected_preset(selected_preset)
{}

const PrinterSettings& SelectedPresetConfigPack::get_printer() const
{
    return std::get<PrinterSettings>(m_selected_preset.printer.values);
}

const PrintSettings& SelectedPresetConfigPack::get_print() const
{
    return std::get<PrintSettings>(m_selected_preset.print.values);
}

const ToolPrintSettings& SelectedPresetConfigPack::get_tool(size_t index) const
{
    return std::get<ToolPrintSettings>(m_selected_preset.tools.at(index).values);
}

const FilamentSettings& SelectedPresetConfigPack::get_filament(size_t index) const
{
    return std::get<FilamentSettings>(m_selected_preset.materials.at(index).values);
}

const size_t SelectedPresetConfigPack::tool_size() const
{
    return m_selected_preset.tools.size();
}

const size_t SelectedPresetConfigPack::filament_size() const
{
    return m_selected_preset.materials.size();
}

} // namespace Slic3r::Domain::Preset
