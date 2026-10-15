#pragma once

#include "Slic3r/Domain/Config.hpp"

#include <map>
#include <string>
#include <variant>

namespace Slic3r::Domain {

using PrintModeParam  = std::variant<bool, int, double, std::string>;
using PrintModeParams = std::map<std::string, PrintModeParam>;

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

/// One layer of macro overrides, keyed in the map by the id of the print mode that produced it.
struct MacroOverridesLayerFDM
{
    /// Display name, kept so a layer can be shown even when its print mode plugin is missing.
    std::string name;
    /// Print mode parameters the overrides were evaluated from.
    PrintModeParams params;
    MacroOverridesFDM overrides;

    bool operator==(const MacroOverridesLayerFDM&) const = default;
};
using MacroOverridesMapFDM = std::map<std::string, MacroOverridesLayerFDM>;

struct MacroOverridesLayerSLA
{
    std::string name;
    PrintModeParams params;
    MacroOverridesSLA overrides;

    bool operator==(const MacroOverridesLayerSLA&) const = default;
};
using MacroOverridesMapSLA = std::map<std::string, MacroOverridesLayerSLA>;

using MacroOverridesMap = std::variant<MacroOverridesMapFDM, MacroOverridesMapSLA>;

Domain::BoxOrBoxesVector to_refs(const Domain::MacroOverridesMapFDM& macro_overrides);

std::string vase_mode_layer_id();

/// The layer applied by vase mode (spiral vase).
MacroOverridesLayerFDM make_vase_mode_layer();

} // namespace Slic3r::Domain
