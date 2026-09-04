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
}
