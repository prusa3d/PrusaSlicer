#include "Slic3r/Domain/SLA/SupportPoint.hpp"

namespace Slic3r::Domain::SLA {

bool SupportPoint::operator==(const SupportPoint& sp) const
{
    float rdiff = std::abs(head_front_radius - sp.head_front_radius);
    return (pos == sp.pos) && rdiff < float(EPSILON) && type == sp.type;
}

bool SupportPoint::operator!=(const SupportPoint& sp) const { return !(sp == (*this)); }

void transform(SupportPoints& points, const Transform3d& trafo)
{
    // Exactly as they are for the identity (common for single part objects), no rounding.
    if (trafo.matrix() == Transform3d::Identity().matrix()) {
        return;
    }
    for (SupportPoint& point : points) {
        point.pos = (trafo * point.pos.cast<double>()).cast<float>();
    }
}
} // namespace Slic3r::Domain::SLA
