#include "Slic3r/Domain/MacroOverrides.hpp"

#include "Slic3r/Domain/ConfigDefsFDM.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Percentage.hpp"

namespace Slic3r::Domain {

MacroOverridesFDM::MacroOverridesFDM() : ConfigBox(get_defs_fdm(), FDMConfigLocation::MacroOverrides) {}

MacroSlotOverridesFDM::MacroSlotOverridesFDM() :
    ConfigBox(get_defs_fdm(), FDMConfigLocation::MacroSlotOverrides)
{}

MacroOverridesSLA::MacroOverridesSLA() : ConfigBox(get_defs_sla(), SLAConfigLocation::MacroOverrides) {}

Domain::BoxOrBoxesVector
to_refs(const Domain::MacroOverridesMapFDM& macro_overrides)
{
    Domain::BoxOrBoxesVector result;
    for (const auto& [_, layer] : macro_overrides) {
        result.push_back(layer.overrides);
    }
    for (const auto& [_, layer] : macro_overrides) {
        if (!layer.slot_overrides.empty()) {
            BoxRefs slot_boxes;
            slot_boxes.insert(
                slot_boxes.end(), layer.slot_overrides.cbegin(), layer.slot_overrides.cend());
            result.push_back(std::move(slot_boxes));
        }
    }
    return result;
}

std::string vase_mode_layer_id()
{
    return "com.prusa3d.slicer.printmodes.vase_mode";
}

MacroOverridesLayerFDM make_vase_mode_layer(const std::size_t slot_count)
{
    MacroOverridesLayerFDM layer;
    layer.name = "Vase mode";
    ConfigOverrides& overrides{layer.overrides.overrides};
    overrides.set("spiral_vase", true);
    overrides.set("support_material", SupportMode::None);
    overrides.set("thin_walls", false);
    overrides.set("avoid_crossing_perimeters", false);
    layer.slot_overrides.resize(slot_count);
    for (MacroSlotOverridesFDM& slot_box : layer.slot_overrides) {
        ConfigOverrides& slot_overrides{slot_box.overrides};
        slot_overrides.set("perimeters", 1);
        slot_overrides.set("top_solid_layers", 0);
        slot_overrides.set("fill_density", Percentage{0});
        slot_overrides.set("support_material_enforce_layers", 0);
        slot_overrides.set("retract_layer_change", false);
    }
    return layer;
}

} // namespace Slic3r::Domain
