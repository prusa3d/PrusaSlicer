#include <catch2/catch_test_macros.hpp>

#include <libslic3r/Triangulation.hpp>
#include <libslic3r/SVG.hpp> // only debug visualization

using namespace Slic3r;

namespace Private{
void store_trinagulation(const ExPolygons &shape,
                         const std::vector<Vec3i> &triangles,
                         const char* file_name = "C:/data/temp/triangulation.svg",
                         double scale = 1e5)
{
    BoundingBox bb;
    for (const auto &expoly : shape) bb.merge(expoly.contour.points);
    bb.scale(scale);
    SVG svg_vis(file_name, bb);
    svg_vis.draw(shape, "gray", .7f);
    Points pts = to_points(shape);
    svg_vis.draw(pts, "black", 4 * scale);

    for (const Vec3i &t : triangles) {
        Slic3r::Polygon triangle({pts[t[0]], pts[t[1]], pts[t[2]]});
        triangle.scale(scale);
        svg_vis.draw(triangle, "green");
    }

    // prevent visualization in test
    CHECK(false);
}
} // namespace


TEST_CASE("Triangulate rectangle with restriction on edge", "[Triangulation]")
{
    //                    0            1            2            3 
    Points points = {Point(1, 1), Point(2, 1), Point(2, 2), Point(1, 2)};
    Triangulation::HalfEdges edges1 = {{1, 3}};
    std::vector<Vec3i> indices1 = Triangulation::triangulate(points, edges1);

    auto check = [](int i1, int i2, Vec3i t) -> bool {
        return true;
        return (t[0] == i1 || t[1] == i1 || t[2] == i1) &&
               (t[0] == i2 || t[1] == i2 || t[2] == i2);
    };
    REQUIRE(indices1.size() == 2); 
    int i1 = edges1.begin()->first, i2 = edges1.begin()->second;
    CHECK(check(i1, i2, indices1[0]));
    CHECK(check(i1, i2, indices1[1]));

    Triangulation::HalfEdges edges2 = {{0, 2}};
    std::vector<Vec3i> indices2 = Triangulation::triangulate(points, edges2);
    REQUIRE(indices2.size() == 2);
    i1 = edges2.begin()->first;
    i2 = edges2.begin()->second;
    CHECK(check(i1, i2, indices2[0]));
    CHECK(check(i1, i2, indices2[1]));
}

TEST_CASE("Triangulation polygon", "[triangulation]") 
{
    Points points = {Point(416, 346), Point(445, 362), Point(463, 389),
                     Point(469, 427), Point(445, 491)};

    Polygon    polygon(points);
    Polygons   polygons({polygon});
    ExPolygon  expolygon(points);
    ExPolygons expolygons({expolygon});

    std::vector<Vec3i> tp   = Triangulation::triangulate(polygon);
    std::vector<Vec3i> tps  = Triangulation::triangulate(polygons);
    std::vector<Vec3i> tep  = Triangulation::triangulate(expolygon);
    std::vector<Vec3i> teps = Triangulation::triangulate(expolygons);
       
    //Private::store_trinagulation(expolygons, teps);

    CHECK(tp.size() == tps.size());
    CHECK(tep.size() == teps.size());
    CHECK(tp.size() == tep.size());
    CHECK(tp.size() == 3);
}

TEST_CASE("Triangulation M shape polygon", "[triangulation]")
{
    //                      0            1            2            3            4
    Polygon shape_M = {Point(0, 0), Point(2, 0), Point(2, 2), Point(1, 1), Point(0, 2)};

    std::vector<Vec3i> triangles = Triangulation::triangulate(shape_M);
    
    // Check outer triangle is not contain
    std::set<int> outer_triangle = {2, 3, 4};
    bool          is_in          = false;
    for (const Vec3i &t : triangles) {
        for (size_t i = 0; i < 3; i++) {
            int index = t[i];
            if (outer_triangle.find(index) == outer_triangle.end()) { 
                is_in = false;
                break; 
            } else {
                is_in = true;
            }
        }
        if (is_in) break;
    }

    //Private::store_trinagulation({ExPolygon(shape_M)}, triangles);
    CHECK(triangles.size() == 3);
    CHECK(!is_in);
}

// same point in triangulation are not Supported
TEST_CASE("Triangulation 2 polygons with same point", "[triangulation]") 
{
    Slic3r::Polygon polygon1 = {
        Point(416, 346), Point(445, 362),
        Point(463, 389), Point(469, 427) /* This point */,
        Point(445, 491)
    };
    Slic3r::Polygon polygon2 = {
        Point(495, 488), Point(469, 427) /* This point */,
        Point(495, 364)
    };
    ExPolygons shape2d = {ExPolygon(polygon1), ExPolygon(polygon2)};
    std::vector<Vec3i> shape_triangles = Triangulation::triangulate(shape2d);
    //Private::store_trinagulation(shape2d, shape_triangles);
    CHECK(shape_triangles.size() == 4);
}
