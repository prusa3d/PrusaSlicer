#ifndef slic3r_ExtrusionRole_hpp_
#define slic3r_ExtrusionRole_hpp_

#include "Slic3r/Domain/GCodeExtrusionRole.hpp"
#include "Slic3r/Domain/enum_bitmask.hpp"

#include <string>
#include <string_view>
#include <cstdint>

namespace Slic3r::Domain {

enum class ExtrusionRoleModifier : uint16_t
{
    // 1) Extrusion types
    // Perimeter (external, inner, ...)
    Perimeter,
    // Infill (top / bottom / solid inner / sparse inner / bridging inner ...)
    Infill,
    // Variable width extrusion
    Thin,
    // Support material extrusion
    Support,
    Skirt,
    Wipe,
    // 2) Extrusion modifiers
    External,
    Solid,
    Ironing,
    Bridge,
    OverBridge,
    // 3) Special types
    // Indicator that the extrusion role was mixed from multiple differing extrusion roles,
    // for example from Support and SupportInterface.
    Mixed,
    // Stopper, there should be maximum 16 modifiers defined for uint16_t bit mask.
    Count
};

} // namespace Slic3r::Domain

namespace Slic3r {

using ExtrusionRoleModifier = Domain::ExtrusionRoleModifier;

// There should be maximum 16 modifiers defined for uint16_t bit mask.
static_assert(int(ExtrusionRoleModifier::Count) <= 16, "ExtrusionRoleModifier: there must be maximum 16 modifiers defined to fit a 16 bit bitmask");

using ExtrusionRoleModifiers = Domain::enum_bitmask<ExtrusionRoleModifier>;
template<> struct Domain::is_enum_bitmask_type<ExtrusionRoleModifier> { static constexpr const bool enable = true; };

struct ExtrusionRole : public ExtrusionRoleModifiers
{
    constexpr ExtrusionRole(const ExtrusionRoleModifier  bit) : ExtrusionRoleModifiers(bit) {}
    constexpr ExtrusionRole(const ExtrusionRoleModifiers bits) : ExtrusionRoleModifiers(bits) {}

    static constexpr const ExtrusionRoleModifiers None{};
    // Internal perimeter, not bridging.
    static constexpr const ExtrusionRoleModifiers Perimeter{ Domain::ExtrusionRoleModifier::Perimeter };
    // External perimeter, not bridging.
    static constexpr const ExtrusionRoleModifiers ExternalPerimeter{ Domain::ExtrusionRoleModifier::Perimeter | Domain::ExtrusionRoleModifier::External };
    // Perimeter, bridging. To be or'ed with ExtrusionRoleModifier::External for external bridging perimeter.
    static constexpr const ExtrusionRoleModifiers OverhangPerimeter{ ExtrusionRoleModifier::Perimeter | ExtrusionRoleModifier::Bridge };
    // Sparse internal infill.
    static constexpr const ExtrusionRoleModifiers InternalInfill{ ExtrusionRoleModifier::Infill };
    // Solid internal infill.
    static constexpr const ExtrusionRoleModifiers SolidInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid };
    // Top solid infill (visible).
    //FIXME why there is no bottom solid infill type?
    static constexpr const ExtrusionRoleModifiers InfillOverBridge{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::OverBridge };
    static constexpr const ExtrusionRoleModifiers TopSolidInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::External };
    // Ironing infill at the top surfaces.
    static constexpr const ExtrusionRoleModifiers Ironing{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Ironing | ExtrusionRoleModifier::External };
    // Visible bridging infill at the bottom of an object.
    static constexpr const ExtrusionRoleModifiers BridgeInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Bridge | ExtrusionRoleModifier::External };
//    static constexpr const ExtrusionRoleModifiers InternalBridgeInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Bridge };
    // Gap fill extrusion, currently used for any variable width extrusion: Thin walls outside of the outer extrusion,
    // gap fill in between perimeters, gap fill between the inner perimeter and infill.
    //FIXME revise GapFill and ThinWall types, split Gap Fill to Gap Fill and ThinWall.
    static constexpr const ExtrusionRoleModifiers GapFill{ ExtrusionRoleModifier::Thin }; // | ExtrusionRoleModifier::External };
//    static constexpr const ExtrusionRoleModifiers ThinWall{ ExtrusionRoleModifier::Thin };
    static constexpr const ExtrusionRoleModifiers Skirt{ ExtrusionRoleModifier::Skirt };
    // Support base material, printed with non-soluble plastic.
    static constexpr const ExtrusionRoleModifiers SupportMaterial{ ExtrusionRoleModifier::Support };
    // Support interface material, printed with soluble plastic.
    static constexpr const ExtrusionRoleModifiers SupportMaterialInterface{ ExtrusionRoleModifier::Support | ExtrusionRoleModifier::External };
    // Wipe tower material.
    static constexpr const ExtrusionRoleModifiers WipeTower{ ExtrusionRoleModifier::Wipe };
    // Extrusion role for a collection with multiple extrusion roles.
    static constexpr const ExtrusionRoleModifiers Mixed{ ExtrusionRoleModifier::Mixed };

    bool is_perimeter() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Perimeter); }
    bool is_external_perimeter() const { return this->is_perimeter() && this->is_external(); }
    bool is_infill() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Infill); }
    bool is_solid_infill() const { return this->is_infill() && this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Solid); }
    bool is_sparse_infill() const { return this->is_infill() && ! this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Solid); }
    bool is_external() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::External); }
    bool is_bridge() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Bridge); }

    bool is_support() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Support); }
    bool is_support_base() const { return this->is_support() && ! this->is_external(); }
    bool is_support_interface() const { return this->is_support() && this->is_external(); }
    bool is_mixed() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Mixed); }

    // Brim is currently marked as skirt.
    bool is_skirt() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Skirt); }
};

// Special flags describing loop
enum ExtrusionLoopRole {
    elrDefault,
    elrContourInternalPerimeter,
    elrSkirt,
};

using Domain::GCodeExtrusionRole;

// Convert a rich bitmask based ExtrusionRole to a less expressive ordinal GCodeExtrusionRole.
// GCodeExtrusionRole is to be serialized into G-code and deserialized by G-code viewer,
GCodeExtrusionRole extrusion_role_to_gcode_extrusion_role(ExtrusionRole role);

std::string gcode_extrusion_role_to_string(GCodeExtrusionRole role);

}

#endif // slic3r_ExtrusionRole_hpp_
