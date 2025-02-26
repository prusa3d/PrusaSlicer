#include <catch2/catch_test_macros.hpp>
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/JumpPointSearch.hpp"

using namespace Slic3r;

TEST_CASE("Test jump point search path finding", "[JumpPointSearch]")
{
    Lines obstacles{};
    obstacles.push_back(Line(Point::new_scale(0, 0), Point::new_scale(50, 50)));
    obstacles.push_back(Line(Point::new_scale(0, 100), Point::new_scale(50, 50)));
    obstacles.push_back(Line(Point::new_scale(0, 0), Point::new_scale(100, 0)));
    obstacles.push_back(Line(Point::new_scale(0, 100), Point::new_scale(100, 100)));
    obstacles.push_back(Line(Point::new_scale(25, -25), Point::new_scale(25, 125)));

    JPSPathFinder jps;
    jps.add_obstacles(obstacles);

    Polyline path = jps.find_path(Point::new_scale(5, 50), Point::new_scale(100, 50));
    path = jps.find_path(Point::new_scale(5, 50), Point::new_scale(150, 50));
    path = jps.find_path(Point::new_scale(5, 50), Point::new_scale(25, 15));
    path = jps.find_path(Point::new_scale(25, 25), Point::new_scale(125, 125));

    // SECTION("Output is empty when source is also the destination") {
    //     bool found = astar::search_route(DummyTracer{}, 0, std::back_inserter(out));
    //     REQUIRE(out.empty());
    //     REQUIRE(found);
    // }

    // SECTION("Return false when there is no route to destination") {
    //     bool found = astar::search_route(DummyTracer{}, 1, std::back_inserter(out));
    //     REQUIRE(!found);
    //     REQUIRE(out.empty());
    // }
}
