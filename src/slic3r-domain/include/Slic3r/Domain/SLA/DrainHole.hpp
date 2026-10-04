#pragma once

#include "Slic3r/Domain/Types.hpp"
#include "admesh/stl.h"

namespace Slic3r::Domain::SLA {

struct DrainHole
{
    Vec3f pos{Vec3f::Zero()};
    Vec3f normal{Vec3f::UnitZ()};
    float radius{5.0f};
    float height{10.0f};
    bool  failed = false;

    bool operator==(const DrainHole &sp) const;

    bool operator!=(const DrainHole &sp) const;

    static constexpr size_t steps = 32;

    // How far a drain hole sticks out of the object surface when it is drilled. The backend shifts
    // the hole out by this much (and makes it deeper to compensate), the gizmo draws it the same way.
    static constexpr float stick_out_length = 1.f;
};

using DrainHoles = std::vector<DrainHole>;

}
