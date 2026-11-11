#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IPrinterPresetSelectorListener.hpp"

#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

namespace Slic3r::Domain::Preset {
struct HwPrinterConfig;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::Preset {

class PresetInteractor;

/**
 * @brief Key under which Connect reports a material slot in the "tools" of a printer.
 *
 * It is the address of the slot, e.g. "3" for the fourth tool or "0.2" for the third slot of
 * a feeder (MMU) on the first tool.
 */
std::string connect_tool_key(const Domain::Preset::HwPrinterConfig& hw_config, size_t slot_index);

/**
 * @brief Selects the material with the given uuid from the material database (MDB) in a slot.
 * @return false when that is not possible, e.g. the material is unknown or no preset matches it.
 */
using MatDbMaterialSelector = std::function<bool(size_t slot_index, const std::string& matdb_uuid)>;

/**
 * @brief Color of a material slot after the Connect material sync.
 *
 * A slot whose filament could not be synchronized keeps its color along with its material.
 */
struct ConnectSlotColor
{
    /// Color Connect reports for the synchronized filament as "#rrggbb", empty when it
    /// reports none.
    std::string color;
    /// The MDB material of the slot changed and Connect reports no color for the filament,
    /// so the color the slot has from its previous material is to be replaced by the
    /// automatic one.
    bool reset_to_auto{false};
};

class PrinterPresetSelector : public WithListeners<IPrinterPresetSelectorListener>
{
public:
    explicit PrinterPresetSelector(PresetInteractor& preset_interactor);

    /**
     * @brief Selects material presets matching the filaments loaded in the Connect printer.
     *
     * A filament reported with an MDB uuid is selected by @a select_matdb_material; its
     * material type is matched only when that is not possible.
     *
     * @return What to do with the colors, one item per material slot.
     */
    std::vector<ConnectSlotColor> select_connect_materials(
        const nlohmann::json& printer_json,
        const MatDbMaterialSelector& select_matdb_material = {}
    );

    void warn(const std::string& msg);

private:
    std::vector<ConnectSlotColor> select_materials(
        const nlohmann::json& printer_json,
        const MatDbMaterialSelector& select_matdb_material,
        std::vector<std::string>& mismatches
    );

    void warn_mismatches(const std::vector<std::string>& mismatches);

private:
    PresetInteractor& m_preset_interactor;
};

} // namespace Slic3r::Biz::Preset
