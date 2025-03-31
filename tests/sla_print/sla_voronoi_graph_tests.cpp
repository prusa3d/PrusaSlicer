#include "sla_test_utils.hpp"
#include <libslic3r/SLA/SupportIslands/VoronoiGraphUtils.hpp>
#include <libslic3r/Geometry/VoronoiVisualUtils.hpp>

using namespace Slic3r;
using namespace Slic3r::sla;

TEST_CASE("Convert coordinate datatype", "[Voronoi]")
{
    using VD                  = Slic3r::Geometry::VoronoiDiagram;
    VD::coordinate_type coord = 101197493902.64694;
    coord_t coord2 = VoronoiGraphUtils::to_coord(coord);
    CHECK(coord2 > 100);

    coord  = -101197493902.64694;
    coord2 = VoronoiGraphUtils::to_coord(coord);
    CHECK(coord2 < -100);

    coord  = 12345.1;
    coord2 = VoronoiGraphUtils::to_coord(coord);
    CHECK(coord2 == 12345);

    coord  = -12345.1;
    coord2 = VoronoiGraphUtils::to_coord(coord);
    CHECK(coord2 == -12345);

    coord  = 12345.9;
    coord2 = VoronoiGraphUtils::to_coord(coord);
    CHECK(coord2 == 12346);

    coord  = -12345.9;
    coord2 = VoronoiGraphUtils::to_coord(coord);
    CHECK(coord2 == -12346);
}

void check(Slic3r::Points points, double max_distance) {
    using VD = Slic3r::Geometry::VoronoiDiagram;
    VD             vd;
    vd.construct_voronoi(points.begin(), points.end());    
    double max_area = M_PI * max_distance*max_distance; // circle = Pi * r^2
    for (const VD::cell_type &cell : vd.cells()) {
        Slic3r::Polygon polygon = VoronoiGraphUtils::to_polygon(cell, points, max_distance);
        CHECK(polygon.area() < max_area);
        CHECK(polygon.contains(points[cell.source_index()]));
    }
}

TEST_CASE("Polygon from cell", "[Voronoi]")
{
    // for debug #define SLA_SVG_VISUALIZATION_CELL_2_POLYGON in VoronoiGraphUtils
    double  max_distance = 1e7;
    coord_t size         = (int) (4e6);
    coord_t half_size         = size/2;
    
    Slic3r::Points two_cols({Point(0, 0), Point(size, 0)});
    check(two_cols, max_distance);

    Slic3r::Points two_rows({Point(0, 0), Point(0, size)});
    check(two_rows, max_distance);

    Slic3r::Points two_diag({Point(0, 0), Point(size, size)});
    check(two_diag, max_distance);

    Slic3r::Points three({Point(0, 0), Point(size, 0), Point(half_size, size)});
    check(three, max_distance);

    Slic3r::Points middle_point({Point(0, 0), Point(size, half_size),
                                 Point(-size, half_size), Point(0, -size)});
    check(middle_point, max_distance);

    Slic3r::Points middle_point2({Point(half_size, half_size), Point(-size, -size), Point(-size, size),
                                  Point(size, -size), Point(size, size)});
    check(middle_point2, max_distance); 

    Slic3r::Points diagonal_points({{-123473762, 71287970},
                                {-61731535, 35684428},
                                {0, 0},
                                {61731535, -35684428},
                                {123473762, -71287970}});
    double diagonal_max_distance = 5e7;
    check(diagonal_points, diagonal_max_distance);
    
    int scale = 10;
    Slic3r::Points diagonal_points2;
    std::transform(diagonal_points.begin(), diagonal_points.end(),
                   std::back_inserter(diagonal_points2),
                   [&](const Slic3r::Point &p) { return p/scale; });
    check(diagonal_points2, diagonal_max_distance / scale);
} 


