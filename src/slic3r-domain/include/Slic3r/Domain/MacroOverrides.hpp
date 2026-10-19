#pragma once

#include "Slic3r/Domain/Config.hpp"

#include <map>
#include <string>
#include <variant>

namespace Slic3r::Domain {

using PrintModeParamValue = std::variant<bool, int, double, std::string>;

/// A print mode parameter as the plugin declared it: the label is what the user sees for it.
struct PrintModeParam
{
    std::string label;
    PrintModeParamValue value;

    bool operator==(const PrintModeParam&) const = default;
};
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

struct MacroOverridesLayerFDM
{
    std::string name;
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
