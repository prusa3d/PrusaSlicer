#include "Slic3r/Domain/MacroOverrides.hpp"

#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Percentage.hpp"

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

MacroOverridesLayer vase_mode_layer()
{
    return {"com.prusa3d.slicer.printmodes.vase_mode", "Vase mode"};
}

MacroOverridesFDM make_vase_mode_overrides()
{
    MacroOverridesFDM overrides;
    overrides.overrides.set("spiral_vase", true);
    overrides.overrides.set("perimeters", 1);
    overrides.overrides.set("top_solid_layers", 0);
    overrides.overrides.set("fill_density", Percentage{0});
    overrides.overrides.set("support_material", SupportMode::None);
    overrides.overrides.set("support_material_enforce_layers", 0);
    overrides.overrides.set("thin_walls", false);
    overrides.overrides.set("retract_layer_change", false);
    overrides.overrides.set("avoid_crossing_perimeters", false);
    return overrides;
}

} // namespace Slic3r::Domain
