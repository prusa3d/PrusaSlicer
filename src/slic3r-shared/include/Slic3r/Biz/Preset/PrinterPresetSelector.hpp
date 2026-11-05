#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IPrinterPresetSelectorListener.hpp"
#include "Slic3r/Biz/Preset/PreferredMaterialTypes.hpp"

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace Slic3r::Biz::Preset {

class PresetInteractor;

class PrinterPresetSelector : public WithListeners<IPrinterPresetSelectorListener>
{
public:
    explicit PrinterPresetSelector(PresetInteractor& preset_interactor);

    /// Returns types of the materials loaded in the Connect printer for the first @p slot_count
    /// material slots.
    static PreferredMaterialTypes
    connect_material_types(const nlohmann::json& printer_json, size_t slot_count);

    /// Warns about material slots whose selected material preset doesn't have the type from
    /// @p material_types.
    void warn_material_mismatches(const PreferredMaterialTypes& material_types);

    void warn(const std::string& msg);

private:
    void warn_mismatches(const std::vector<std::string>& mismatches);

private:
    PresetInteractor& m_preset_interactor;
};

} // namespace Slic3r::Biz::Preset
