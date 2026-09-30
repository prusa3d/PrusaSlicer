#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IPrinterPresetSelectorListener.hpp"

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace Slic3r::Domain::Preset {
struct HwPrinterConfig;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Preset {

class PresetInteractor;

class PrinterPresetSelector : public WithListeners<IPrinterPresetSelectorListener>
{
public:
    explicit PrinterPresetSelector(PresetInteractor& preset_interactor);

    /// Keeps the current printer preset when it already uses this hardware config.
    bool select_for_hw_config(const Domain::Preset::HwPrinterConfig& hw_config);

    bool select_for_connect_printer(const nlohmann::json& printer_json);

private:
    void select_tools(const nlohmann::json& printer_json, std::vector<std::string>& mismatches);

    void select_materials(const nlohmann::json& printer_json, std::vector<std::string>& mismatches);

    void warn(const std::string& msg);

private:
    PresetInteractor& m_preset_interactor;
};

} // namespace Slic3r::Biz::Preset
