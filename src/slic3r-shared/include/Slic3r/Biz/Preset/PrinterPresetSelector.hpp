#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Preset/IPrinterPresetSelectorListener.hpp"
#include "Slic3r/Biz/Preset/PreferredMaterialTypes.hpp"

#include <functional>
#include <optional>
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
 * @brief Tells whether the material preset selected in a slot is one the material selection
 *        offers for the material from the material database (MDB).
 */
using MatDbMaterialMatcher =
    std::function<bool(size_t slot_index, const Domain::MatDb::Material& material)>;

/**
 * @brief Filament Connect reports in a material slot.
 */
struct ConnectMaterial
{
    /// Material type, the abbreviation when Connect reports no type. Empty when the slot is not
    /// reported, is empty or has neither.
    std::string type;
    /// Uuid of the material in the material database (MDB), empty when Connect reports none.
    std::string matdb_uuid;
    /// The material with matdb_uuid from MDB, empty when MDB does not know it. Filled by the
    /// caller of PrinterPresetSelector::connect_materials(), which only reads the printer.
    std::optional<Domain::MatDb::Material> matdb_material;
    /// Color as "#rrggbb", empty when Connect reports none or a malformed one.
    std::string color;
};

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
     * @brief Returns the filaments loaded in the Connect printer, one item per material slot of
     *        @p hw_config.
     */
    static std::vector<ConnectMaterial> connect_materials(
        const nlohmann::json& printer_json,
        const Domain::Preset::HwPrinterConfig& hw_config
    );

    /// Returns the materials preferred by the printer switch, one item per material slot.
    static PreferredMaterialTypes material_types(const std::vector<ConnectMaterial>& materials);

    /**
     * @brief Completes the selection of the Connect filaments after the printer switch, which
     *        selected their material presets and MDB materials (see material_types()).
     *
     * Checks which slots hold the reported filaments: those holding a preset
     * @p matches_matdb_material accepts for the reported MDB material, and the others holding
     * a preset of the reported type. The other slots with a reported type are warned about,
     * see warn_material_mismatches().
     *
     * @param previous_matdb_uuids  MDB uuids of the slots before the printer switch.
     * @return What to do with the colors, one item per material slot.
     */
    std::vector<ConnectSlotColor> complete_connect_materials(
        const std::vector<ConnectMaterial>& materials,
        const std::vector<std::string>& previous_matdb_uuids,
        const MatDbMaterialMatcher& matches_matdb_material
    );

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
