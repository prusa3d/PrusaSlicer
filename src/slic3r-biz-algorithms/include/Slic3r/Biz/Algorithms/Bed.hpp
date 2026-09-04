#pragma once

#include "Slic3r/Domain/Bed.hpp"
#include <Slic3r/Biz/Algorithms/AABBMesh.hpp>

namespace Slic3r::Domain {
struct BedInstance;
class Polygon;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Algorithms::Bed {

enum class BedContainmentState
{
    // Inside the build volume, thus printable.
    Inside,
    // Colliding with the build volume boundary, thus not printable and error is shown.
    Colliding,
    // Outside of the build volume means the object is ignored: Not printed and no error is shown.
    Outside,
    // Completely below the print bed. The same as Outside, but an object with one printable part below the print bed
    // and at least one part above the print bed is still printable.
    Below,
};

struct ObjectCollisionData
{
    Domain::BoundingBox3d bounding_box;
    Domain::Vec2ds convex_hull_2d;

    void translate(const Domain::Vec3d& shift);
};

/**
 * @brief 2D collision data for wipe tower containment detection.
 *
 * The footprint is the union of the rectangle (with brim) and the stabilization
 * cone ellipse, or their convex hull for convex bed types.
 */
struct WipeTowerCollisionData
{
    Domain::BoundingBox2d bounding_box;
    Domain::Vec2ds footprint_2d;
};

struct BedInstanceCollisionData
{
    const Domain::BedInstance& instance;
    const AABBMesh* aabb_mesh{nullptr};
    const Domain::Polygon* scaled_bed_contour{nullptr};

    BedInstanceCollisionData(
        const Domain::BedInstance& bed_instance,
        const AABBMesh* bed_aabb_mesh         = nullptr,
        const Domain::Polygon* scaled_contour = nullptr
    ) :
        instance(bed_instance),
        aabb_mesh(bed_aabb_mesh),
        scaled_bed_contour(scaled_contour)
    {}

    Domain::Vec2d instance_offset() const;
};

Domain::BedType detect_bed_type_from_contour(const Domain::Vec2ds& contour);
Domain::Vec2ds bed_contour_as_triangles(const Domain::Bed& bed);
indexed_triangle_set bed_contour_as_its(const Domain::Vec2ds& contour);
AABBMesh bed_contour_as_aabb_mesh(const Domain::Bed& bed);

BedContainmentState contains_2d(
    const BedInstanceCollisionData& bed_instance,
    const Domain::BoundingBox2d& object_bounding_box,
    const Domain::Vec2ds& object_convex_hull
);
BedContainmentState contains_3d(
    const BedInstanceCollisionData& bed_instance,
    const ObjectCollisionData& collision_data
);

} // namespace Slic3r::Biz::Algorithms::Bed
