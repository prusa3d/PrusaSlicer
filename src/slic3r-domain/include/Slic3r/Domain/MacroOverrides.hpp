#pragma once

#include "Slic3r/Domain/Config.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace Slic3r::Domain {

// Order matters, higher value layers are applied last.
enum class MacroOverridesLayerFDM
{
    SpiralVase
};

enum class MacroOverridesLayerSLA
{
};

class MacroOverridesFDM : public ConfigBox
{
public:
    MacroOverridesFDM();
};

class MacroOverridesSLA : public ConfigBox
{
public:
    MacroOverridesSLA();
};

using MacroOverridesMapFDM = std::map<MacroOverridesLayerFDM, MacroOverridesFDM>;
using MacroOverridesMapSLA = std::map<MacroOverridesLayerSLA, MacroOverridesSLA>;
using MacroOverridesMap    = std::variant<MacroOverridesMapFDM, MacroOverridesMapSLA>;

Domain::BoxOrBoxesVector to_refs(const Domain::MacroOverridesMapFDM& macro_overrides);

/// Serialization name of the layer (key in the "macro_overrides" object).
std::string to_string(MacroOverridesLayerFDM layer);
std::optional<MacroOverridesLayerFDM> macro_overrides_layer_fdm_from_string(std::string_view name);

/// The overrides applied by the SpiralVase layer (vase mode).
MacroOverridesFDM make_vase_mode_overrides();

} // namespace Slic3r::Domain
