#include "Slic3r/Biz/Algorithms/Bed.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Domain/BedInstance.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz;

using Slic3r::Biz::Algorithms::Bed::bed_contour_as_aabb_mesh;
using Slic3r::Biz::Algorithms::Bed::bed_contour_as_its;
using Slic3r::Biz::Algorithms::Bed::BedContainmentState;
using Slic3r::Biz::Algorithms::Bed::BedInstanceCollisionData;
using Slic3r::Biz::Algorithms::Bed::contains_2d;
using Slic3r::Domain::Bed;
using Slic3r::Domain::BedCreationData;
using Slic3r::Domain::BedInstance;
using Slic3r::Domain::BedType;
using Slic3r::Domain::BoundingBox2d;
using Slic3r::Domain::Polygon;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec2ds;

namespace {

Bed make_rectangular_bed(const double width, const double height)
{
    const Vec2ds contour = {
        {-width / 2, -height / 2},
        {width / 2, -height / 2},
        {width / 2, height / 2},
        {-width / 2, height / 2}
    };

    return Bed::create(
        BedCreationData{BedType::Rectangle, contour, bed_contour_as_its(contour), 250.0f}
    );
}

Bed make_convex_bed(const int vertex_count, const double radius = 120.0)
{
    Vec2ds contour;
    for (int vertex_idx = 0; vertex_idx < vertex_count; ++vertex_idx) {
        double angle = 2.0 * std::numbers::pi * vertex_idx / vertex_count + std::numbers::pi / 2.0;
        contour.push_back({radius * std::cos(angle), radius * std::sin(angle)});
    }

    return Bed::create(
        BedCreationData{BedType::Convex, contour, bed_contour_as_its(contour), 250.0f}
    );
}

Bed make_l_shaped_bed()
{
    const Vec2ds contour = {{-60, -60}, {0, -60}, {0, 0}, {60, 0}, {60, 60}, {-60, 60}};
    return Bed::create(
        BedCreationData{BedType::Custom, contour, bed_contour_as_its(contour), 250.0f}
    );
}

BedContainmentState check_containment(
    const Bed& bed,
    const AABBMesh& mesh,
    const BoundingBox2d& object_bounding_box,
    const Vec2ds& object_convex_hull
)
{
    const BedInstance bed_instance(bed);
    const Polygon scaled_contour = Algorithms::Polygon::scaled(bed.contour());
    const BedInstanceCollisionData collision_data(bed_instance, &mesh, &scaled_contour);
    return contains_2d(collision_data, object_bounding_box, object_convex_hull);
}

} // namespace

TEST_CASE("Object fully inside rectangular bed", "[algorithms][bed-containment]")
{
    // 200x200 bed centered at origin (extends from -100 to 100 on both axes).
    Bed bed       = make_rectangular_bed(200.0, 200.0);
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Small 20x20 object at a center.
    Vec2ds convex_hull         = {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Inside);
}

TEST_CASE("Object fully outside rectangular bed", "[algorithms][bed-containment]")
{
    // 200x200 bed centered at origin (extends from -100 to 100 on both axes).
    Bed bed       = make_rectangular_bed(200.0, 200.0);
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Object far away from bed.
    Vec2ds convex_hull         = {{300, 300}, {320, 300}, {320, 320}, {300, 320}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Outside);
}

TEST_CASE("Object partially inside rectangular bed", "[algorithms][bed-containment]")
{
    // 200x200 bed centered at origin (extends from -100 to 100 on both axes).
    Bed bed       = make_rectangular_bed(200.0, 200.0);
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Object crosses the right edge: half in, half out
    Vec2ds convex_hull         = {{80, -10}, {120, -10}, {120, 10}, {80, 10}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(
        check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Colliding
    );
}

TEST_CASE("All vertices outside but edges cross rectangular bed", "[algorithms][bed-containment]")
{
    // 200x200 bed centered at origin (extends from -100 to 100 on both axes).
    Bed bed       = make_rectangular_bed(200.0, 200.0);
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Wide thin object near the top edge of the bed.
    // Each vertex has at least one coordinate outside the bed,
    // but the object's interior overlaps with the bed in the region [-100,100] x [95,100].
    Vec2ds convex_hull         = {{-110, 95}, {110, 95}, {110, 105}, {-110, 105}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    // All convex hull vertices should be outside the bed, but edges cross the bed boundary.
    CHECK(
        check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Colliding
    );
}

TEST_CASE("Object fully inside convex pentagonal bed", "[algorithms][bed-containment]")
{
    Bed bed       = make_convex_bed(5);
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Small object at a center.
    Vec2ds convex_hull         = {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Inside);
}

TEST_CASE(
    "All vertices outside but edges cross convex pentagonal bed",
    "[algorithms][bed-containment]"
)
{
    // Convex pentagonal bed with radius 120, top vertex at (0, 120).
    Bed bed       = make_convex_bed(5);
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Wide thin object (400x10) near the top vertex of the pentagon.
    // All 4 vertices are outside, but the object's edges cross the pentagon boundary.
    Vec2ds convex_hull         = {{-200, 115}, {200, 115}, {200, 125}, {-200, 125}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    // All 4 vertices are outside the pentagon, but the object's edges cross the pentagon edges.
    CHECK(
        check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Colliding
    );
}

TEST_CASE("Object fully inside L-shaped custom bed", "[algorithms][bed-containment]")
{
    Bed bed       = make_l_shaped_bed();
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Small object in the wide top part of the L.
    Vec2ds convex_hull         = {{-10, 10}, {10, 10}, {10, 30}, {-10, 30}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Inside);
}

TEST_CASE("Object fully outside L-shaped custom bed", "[algorithms][bed-containment]")
{
    Bed bed       = make_l_shaped_bed();
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Object far from a bed.
    Vec2ds convex_hull         = {{200, 200}, {220, 200}, {220, 220}, {200, 220}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Outside);
}

TEST_CASE("Object in concavity of L-shaped custom bed", "[algorithms][bed-containment]")
{
    Bed bed       = make_l_shaped_bed();
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Object is in the concave region of the L (bottom-right quadrant, x=10..50, y=-50..-10).
    // This area falls within the bed's bounding box, but outside the actual L-shaped contour.
    Vec2ds convex_hull         = {{10, -50}, {50, -50}, {50, -10}, {10, -10}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Outside);
}

TEST_CASE("Object crosses outer edge of L-shaped custom bed", "[algorithms][bed-containment]")
{
    Bed bed       = make_l_shaped_bed();
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Object crosses the outer top edge of the L: partially inside, partially outside.
    Vec2ds convex_hull         = {{-20, 40}, {20, 40}, {20, 80}, {-20, 80}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(
        check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Colliding
    );
}

TEST_CASE("Object crosses concave edge of L-shaped custom bed", "[algorithms][bed-containment]")
{
    Bed bed       = make_l_shaped_bed();
    AABBMesh mesh = bed_contour_as_aabb_mesh(bed);

    // Object extends from inside the L across the concave boundary into the concave region.
    // Left part (x=-20..0) is inside the narrow leg, right part (x=0..40) is in the concave region.
    Vec2ds convex_hull         = {{-20, -50}, {40, -50}, {40, -10}, {-20, -10}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(
        check_containment(bed, mesh, bounding_box, convex_hull) == BedContainmentState::Colliding
    );
}

TEST_CASE("Object is colliding with two beds", "[algorithms][bed-containment]")
{
    const Vec2ds contour1 = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    Bed bed1 = Bed::create(
        BedCreationData{BedType::Rectangle, contour1, bed_contour_as_its(contour1), 250.0f}
    );

    const Vec2ds contour2 = {{20, 0}, {30, 0}, {30, 10}, {20, 10}};

    Bed bed2 = Bed::create(
        BedCreationData{BedType::Rectangle, contour2, bed_contour_as_its(contour2), 250.0f}
    );

    AABBMesh mesh1 = bed_contour_as_aabb_mesh(bed1);
    AABBMesh mesh2 = bed_contour_as_aabb_mesh(bed2);

    Vec2ds convex_hull         = {{0, 0}, {40, 0}, {40, 10}, {0, 10}};
    BoundingBox2d bounding_box = Algorithms::BoundingBox::construct(convex_hull);

    CHECK(
        check_containment(bed1, mesh1, bounding_box, convex_hull) == BedContainmentState::Colliding
    );
    CHECK(
        check_containment(bed2, mesh2, bounding_box, convex_hull) == BedContainmentState::Colliding
    );
}
