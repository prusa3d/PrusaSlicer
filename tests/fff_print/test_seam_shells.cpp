#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <filesystem>
#include <fstream>
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/GCode/SeamPainting.hpp"
#include "test_data.hpp"

#include "libslic3r/GCode/SeamShells.hpp"

using namespace Slic3r;
using namespace Slic3r::Seams;
using namespace Catch;

struct ProjectionFixture
{
    Polygon extrusion_path{
        Point{scaled(Vec2d{-1.0, -1.0})}, Point{scaled(Vec2d{1.0, -1.0})},
        Point{scaled(Vec2d{1.0, 1.0})}, Point{scaled(Vec2d{-1.0, 1.0})}};

    ExPolygon island_boundary;
    Seams::Geometry::Extrusions extrusions;
    double extrusion_width{0.2};

    ProjectionFixture() {
        extrusions.emplace_back(
            Polygon{extrusion_path},
            extrusion_path.bounding_box(),
            extrusion_width, island_boundary,
            Seams::Geometry::Overhangs{}
        );
    }
};

TEST_CASE_METHOD(ProjectionFixture, "Project to geometry matches", "[Seams][SeamShells]") {
    Polygon boundary_polygon{extrusion_path};
    // Add + 0.1 to check that boundary polygon has been picked.
    boundary_polygon.scale(1.0 + extrusion_width / 2.0 + 0.1);
    island_boundary.contour = boundary_polygon;

    Seams::Geometry::BoundedPolygons result{Seams::Geometry::project_to_geometry(extrusions, 5.0)};
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].polygon.size() == 4);
    // Boundary polygon is picked.
    CHECK(result[0].polygon[0].x() == Approx(scaled(-(1.0 + extrusion_width / 2.0 + 0.1))));
}

TEST_CASE_METHOD(ProjectionFixture, "Project to geometry does not match", "[Seams][SeamShells]") {
    Polygon boundary_polygon{extrusion_path};

    // Island boundary is far from the extrusion.
    boundary_polygon.scale(5.0);

    island_boundary.contour = boundary_polygon;

    Seams::Geometry::BoundedPolygons result{Seams::Geometry::project_to_geometry(extrusions, 1.0)};
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].polygon.size() == 4);

    const Polygon expanded{expand(extrusions.front().polygon, scaled(extrusion_width / 2.0)).front()};

    // The extrusion is expanded and returned.
    CHECK(result[0].polygon == expanded);
}
