#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IPrinterPresetSelectorListener.hpp"

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace Slic3r::Biz::Preset {

class PresetInteractor;

class PrinterPresetSelector : public WithListeners<IPrinterPresetSelectorListener>
{
public:
    explicit PrinterPresetSelector(PresetInteractor& preset_interactor);

    /// Selects material presets matching the filaments loaded in the Connect printer.
    void select_connect_materials(const nlohmann::json& printer_json);

    void warn(const std::string& msg);

private:
    void select_materials(const nlohmann::json& printer_json, std::vector<std::string>& mismatches);

    void warn_mismatches(const std::vector<std::string>& mismatches);

private:
    PresetInteractor& m_preset_interactor;
};

} // namespace Slic3r::Biz::Preset
