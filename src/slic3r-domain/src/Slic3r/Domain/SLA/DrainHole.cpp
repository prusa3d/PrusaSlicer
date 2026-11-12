#include "Slic3r/Domain/SLA/DrainHole.hpp"
#include "Slic3r/Domain/Constants.hpp"

namespace Slic3r::Domain::SLA {

bool DrainHole::operator==(const DrainHole &sp) const
{
    return (pos == sp.pos) && (normal == sp.normal) &&
            is_approx(radius, sp.radius) &&
            is_approx(height, sp.height);
}

bool DrainHole::operator!=(const DrainHole &sp) const { return !(sp == (*this)); }

void transform(DrainHoles& holes, const Transform3d& trafo)
{
    // Exactly as they are for the identity (common for single part objects), no rounding.
    if (trafo.matrix() == Transform3d::Identity().matrix()) {
        return;
    }
    for (DrainHole& hole : holes) {
        hole.pos    = (trafo * hole.pos.cast<double>()).cast<float>();
        hole.normal = (trafo.linear() * hole.normal.cast<double>()).normalized().cast<float>();
    }
}
}
