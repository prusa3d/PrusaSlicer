#pragma once

#include "Slic3r/Domain/Config.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace Slic3r::Domain {

struct MacroOverridesLayer
{
    std::string id;
    std::string name;

    std::strong_ordering operator<=>(const MacroOverridesLayer& other) const
    {
        return id <=> other.id;
    }

    bool operator==(const MacroOverridesLayer& other) const
    {
        return id == other.id;
    }
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

using MacroOverridesMapFDM = std::map<MacroOverridesLayer, MacroOverridesFDM>;
using MacroOverridesMapSLA = std::map<MacroOverridesLayer, MacroOverridesSLA>;
using MacroOverridesMap    = std::variant<MacroOverridesMapFDM, MacroOverridesMapSLA>;

Domain::BoxOrBoxesVector to_refs(const Domain::MacroOverridesMapFDM& macro_overrides);

MacroOverridesLayer vase_mode_layer();

/// The overrides applied by the SpiralVase layer (vase mode).
MacroOverridesFDM make_vase_mode_overrides();

} // namespace Slic3r::Domain
