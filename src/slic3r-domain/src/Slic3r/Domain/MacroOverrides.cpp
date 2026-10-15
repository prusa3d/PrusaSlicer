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
    for (const auto& [_, macro_override] : macro_overrides) {
        result.push_back(macro_override.overrides);
    }
    return result;
}

std::string vase_mode_layer_id()
{
    return "com.prusa3d.slicer.printmodes.vase_mode";
}

MacroOverridesLayerFDM make_vase_mode_layer()
{
    MacroOverridesLayerFDM layer;
    layer.name = "Vase mode";
    ConfigOverrides& overrides{layer.overrides.overrides};
    overrides.set("spiral_vase", true);
    overrides.set("perimeters", 1);
    overrides.set("top_solid_layers", 0);
    overrides.set("fill_density", Percentage{0});
    overrides.set("support_material", SupportMode::None);
    overrides.set("support_material_enforce_layers", 0);
    overrides.set("thin_walls", false);
    overrides.set("retract_layer_change", false);
    overrides.set("avoid_crossing_perimeters", false);
    return layer;
}

} // namespace Slic3r::Domain
