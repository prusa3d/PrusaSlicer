#include "Slic3r/Biz/Algorithms/ExPolygonsWithId.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r::Biz::Algorithms::ExPolygonsWithId {

void translate(Domain::ExPolygonsWithIds& expolygons_with_ids, const Domain::Point& p)
{
    for (Domain::ExPolygonsWithId& expolygons_with_id : expolygons_with_ids)
        Algorithms::ExPolygon::translate(expolygons_with_id.expoly, p);
}

Domain::BoundingBox2crd get_extents(const Domain::ExPolygonsWithIds& expolygons_with_ids)
{
    Domain::BoundingBox2crd bb;
    for (const Domain::ExPolygonsWithId& expolygons_with_id : expolygons_with_ids)
        bb = Algorithms::BoundingBox::merge(
            bb,
            Algorithms::ExPolygon::get_extents(expolygons_with_id.expoly)
        );
    return bb;
}

void center(Domain::ExPolygonsWithIds& e)
{
    Domain::BoundingBox2crd bb = get_extents(e);
    translate(e, -Algorithms::BoundingBox::center(bb));
}

} // namespace Slic3r::Biz::Algorithms::ExPolygonsWithId
