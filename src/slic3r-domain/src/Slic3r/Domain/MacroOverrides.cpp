#include "Slic3r/Domain/MacroOverrides.hpp"

#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Domain {

MacroOverridesFDM::MacroOverridesFDM() : ConfigBox(get_defs_fdm(), FDMConfigLocation::MacroOverrides) {}

MacroOverridesSLA::MacroOverridesSLA() : ConfigBox(get_defs_sla(), SLAConfigLocation::MacroOverrides) {}

Domain::BoxOrBoxesVector
to_refs(const Domain::MacroOverridesMapFDM& macro_overrides)
{
    Domain::BoxOrBoxesVector result;
    for (const auto& [layer, macro_override] : macro_overrides) {
        result.push_back(macro_override);
    }
    return result;
}

std::string to_string(MacroOverridesLayerFDM layer)
{
    switch (layer) {
    case MacroOverridesLayerFDM::SpiralVase: return "vase_mode";
    }
    PANIC("Unknown macro overrides layer");
}

std::optional<MacroOverridesLayerFDM> macro_overrides_layer_fdm_from_string(std::string_view name)
{
    if (name == "vase_mode") {
        return MacroOverridesLayerFDM::SpiralVase;
    }
    return std::nullopt;
}

} // namespace Slic3r::Domain
