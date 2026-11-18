#pragma once

#include <string>
#include <vector>

namespace Slic3r::Biz::Preset {

/**
 * Material (filament) type per material slot, which is preferred when material presets are
 * selected after a printer switch (e.g. materials loaded in a Connect printer).
 * Empty type means no preference for the slot.
 */
using PreferredMaterialTypes = std::vector<std::string>;

} // namespace Slic3r::Biz::Preset
