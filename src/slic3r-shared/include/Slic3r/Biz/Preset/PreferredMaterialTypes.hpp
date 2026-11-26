#pragma once

#include "Slic3r/Domain/MatDb/Material.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::Preset {

/**
 * @brief Material (filament) preferred in a material slot.
 */
struct PreferredMaterial
{
    /// Material type, empty for no preference by type.
    std::string type;
    /// Uuid of the material in the material database (MDB), empty when there is none. It is
    /// stored with the preferred preset and replaces the MDB material of the slot.
    std::string matdb_uuid;
    /// The material with matdb_uuid, when MDB knows it. The preset its MDB match rules rank
    /// highest is preferred over the type.
    std::optional<Domain::MatDb::Material> matdb_material;
};

/**
 * @brief Material (filament) per material slot, which is preferred when material presets are
 * selected after a printer switch (e.g. materials loaded in a Connect printer).
 * Empty type and no MDB material means no preference for the slot.
 */
using PreferredMaterialTypes = std::vector<PreferredMaterial>;

} // namespace Slic3r::Biz::Preset
