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
};

using DrainHoles = std::vector<DrainHole>;

}
