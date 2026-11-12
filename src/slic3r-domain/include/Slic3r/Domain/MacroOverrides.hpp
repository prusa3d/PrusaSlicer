#pragma once

#include "Slic3r/Domain/Config.hpp"

#include <map>
#include <string>
#include <variant>
#include <vector>

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

class MacroSlotOverridesFDM : public ConfigBox
{
public:
    MacroSlotOverridesFDM();
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
    std::vector<MacroSlotOverridesFDM> slot_overrides;

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

/// The layer applied by vase mode (spiral vase), with one slot box per material slot.
MacroOverridesLayerFDM make_vase_mode_layer(std::size_t slot_count);

} // namespace Slic3r::Domain
