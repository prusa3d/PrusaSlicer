// Based on implementation by @platsch

#ifndef slic3r_Slicing_hpp_
#define slic3r_Slicing_hpp_

#include <vector>
#include <utility>
#include <cassert>

#include "Slic3r/Domain/LayerHeightProfile.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"

namespace Slic3r
{

class PrintConfigView;
class PrintObjectConfigView;
class DynamicPrintConfig;

namespace Domain {
struct ConfigPackFDM;
class ObjectSettings;
}

// Parameters to guide object slicing and support generation.
// The slicing parameters account for a raft and whether the 1st object layer is printed with a normal or a bridging flow
// (using a normal flow over a soluble support, using a bridging flow over a non-soluble support).
struct SlicingParameters
{
	SlicingParameters() = default;

    static SlicingParameters create_from_config(
        const PrintObjectConfigView& config,
        double object_height,
        const std::vector<unsigned int>& object_extruders,
        const Domain::Vec3d& object_shrinkage_compensation
    );

    static SlicingParameters create_from_config(
        const Domain::ConfigPackFDM& config,
        const Domain::ObjectSettings& object_settings,
        double object_height,
        const std::vector<unsigned int>& object_extruders,
        const Domain::Preset::HwPrinterConfig& hw_config
    );

    // Has any raft layers?
    bool        has_raft() const { return raft_layers() > 0; }
    size_t      raft_layers() const { return base_raft_layers + interface_raft_layers; }

    // Is the 1st object layer height fixed, or could it be varied?
    bool        first_object_layer_height_fixed()  const { return ! has_raft() || first_object_layer_bridging; }

    // Height of the object to be printed. This value does not contain the raft height.
    // This value is scaled by shrinkage compensation in the Z-axis.
    double    object_print_z_height() const { return object_print_z_max - object_print_z_min; }

    // Height of the object to be printed. This value does not contain the raft height.
    // This value isn't scaled by shrinkage compensation in the Z-axis.
    double    object_print_z_uncompensated_height() const { return object_print_z_uncompensated_max - object_print_z_min; }

    bool        valid { false };

    // Number of raft layers.
    size_t      base_raft_layers { 0 };
    // Number of interface layers including the contact layer.
    size_t      interface_raft_layers { 0 };

    // Layer heights of the raft (base, interface and a contact layer).
    double    base_raft_layer_height { 0 };
    double    interface_raft_layer_height { 0 };
    double    contact_raft_layer_height { 0 };

	// The regular layer height, applied for all but the first layer, if not overridden by layer ranges
	// or by the variable layer thickness table.
    double    layer_height { 0 };
    // Minimum / maximum layer height, to be used for the automatic adaptive layer height algorithm,
    // or by an interactive layer height editor.
    double    min_layer_height { 0 };
    double    max_layer_height { 0 };
    double    max_suport_layer_height { 0 };

    // First layer height of the print, this may be used for the first layer of the raft
    // or for the first layer of the print.
    double    first_print_layer_height { 0 };

    // Thickness of the first layer. This is either the first print layer thickness if printed without a raft,
    // or a bridging flow thickness if printed over a non-soluble raft,
    // or a normal layer height if printed over a soluble raft.
    double    first_object_layer_height { 0 };

    // If the object is printed over a non-soluble raft, the first layer may be printed with a briding flow.
    bool 		first_object_layer_bridging { false };

    // Soluble interface? (PLA soluble in water, HIPS soluble in lemonen)
    // otherwise the interface must be broken off.
    bool        soluble_interface { false };
    // Gap when placing object over raft.
    double    gap_raft_object { 0 };
    // Gap when placing support over object.
    double    gap_object_support { 0 };
    // Gap when placing object over support.
    double    gap_support_object { 0 };

    // Bottom and top of the printed object.
    // If printed without a raft, object_print_z_min = 0 and object_print_z_max = object height.
    // Otherwise object_print_z_min is equal to the raft height.
    double    raft_base_top_z { 0 };
    double    raft_interface_top_z { 0 };
    double    raft_contact_top_z { 0 };
    // In case of a soluble interface, object_print_z_min == raft_contact_top_z, otherwise there is a gap between the raft and the 1st object layer.
    double 	object_print_z_min { 0 };
    // This value of maximum print Z is scaled by shrinkage compensation in the Z-axis.
    double 	object_print_z_max { 0 };

    // This value of maximum print Z isn't scaled by shrinkage compensation.
    double 	object_print_z_uncompensated_max { 0 };
    // Scaling factor for compensating shrinkage in Z-axis.
    double    object_shrinkage_compensation_z { 0 };
};
static_assert(std::is_trivially_copyable_v<SlicingParameters>, "SlicingParameters class is not POD (and it should be - see constructor).");

// The two slicing parameters lead to the same layering as long as the variable layer thickness is not in action.
inline bool equal_layering(const SlicingParameters &sp1, const SlicingParameters &sp2)
{
    assert(sp1.valid);
    assert(sp2.valid);
    return  sp1.base_raft_layers                    == sp2.base_raft_layers                     &&
            sp1.interface_raft_layers               == sp2.interface_raft_layers                &&
            sp1.base_raft_layer_height              == sp2.base_raft_layer_height               &&
            sp1.interface_raft_layer_height         == sp2.interface_raft_layer_height          &&
            sp1.contact_raft_layer_height           == sp2.contact_raft_layer_height            &&
            sp1.layer_height                        == sp2.layer_height                         &&
//            sp1.max_suport_layer_height             == sp2.max_suport_layer_height              &&
            sp1.first_print_layer_height            == sp2.first_print_layer_height             &&
            sp1.first_object_layer_height           == sp2.first_object_layer_height            &&
            sp1.first_object_layer_bridging         == sp2.first_object_layer_bridging          &&
            sp1.soluble_interface                   == sp2.soluble_interface                    &&
            sp1.gap_raft_object                     == sp2.gap_raft_object                      &&
            sp1.gap_object_support                  == sp2.gap_object_support                   &&
            sp1.gap_support_object                  == sp2.gap_support_object                   &&
            sp1.raft_base_top_z                     == sp2.raft_base_top_z                      &&
            sp1.raft_interface_top_z                == sp2.raft_interface_top_z                 &&
            sp1.raft_contact_top_z                  == sp2.raft_contact_top_z                   &&
            sp1.object_print_z_min                  == sp2.object_print_z_min;
}

Domain::ZHeightPairs layer_height_profile_from_ranges(
    const SlicingParameters& slicing_params,
    const Domain::LayerConfigRanges& layer_config_ranges
);

// Produce object layers as LayerZRange boundaries.
// The object layers are based at z=0, ignoring the raft layers.
Domain::LayerZRanges generate_object_layers(
    const SlicingParameters& slicing_params,
    const Domain::ZHeightPairs& layer_height_profile
);

} // namespace Slic3r

#endif /* slic3r_Slicing_hpp_ */
