#pragma once

#include "Slic3r/Domain/IConfigPackFDMViewer.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

namespace Slic3r::Domain::Preset {

class SelectedPresetConfigPack : public IConfigPackFDMViewer
{
public:
    explicit SelectedPresetConfigPack(const SelectedPreset& selected_preset);

    const PrinterSettings& get_printer() const override;
    const PrintSettings& get_print() const override;
    const ToolPrintSettings& get_tool(size_t index) const override;
    const FilamentSettings& get_filament(size_t index) const override;
    const size_t tool_size() const override;
    const size_t filament_size() const override;

private:
    const SelectedPreset& m_selected_preset;
};

} // namespace Slic3r::Domain::Preset
