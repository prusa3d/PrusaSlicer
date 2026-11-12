#include "Slic3r/Biz/Emboss/ShapeProvider.hpp"

namespace Slic3r::Biz::Emboss {

void ShapeProvider::write(Domain::ModelVolume& volume) const
{
    volume.emboss_shape = m_shape;

    // Fix for object: stored attribute that volume use surface when it is object
    // Can appear when remove source volume in the object
    if (volume.is_the_only_one_part() && m_shape.projection.use_surface) {
        volume.emboss_shape->projection.use_surface = false;
    }
}

bool ShapeProvider::create_shape_with_union()
{
    if (!create_shape())
        return false;

    // IMPROVE: use real size of volume for union delta value
    // ... need world matrix for volume
    // ... printer resolution will be fine too
    union_with_delta(m_shape, UNION_DELTA, UNION_MAX_ITERATIN);
    return !m_shape.final_shape.expolygons.empty();
}

} // namespace Slic3r::Biz::Emboss
