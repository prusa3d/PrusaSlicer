#include <catch2/catch_test_macros.hpp>
#include "libslic3r/JumpPointSearch.hpp"

using namespace Slic3r;

TEST_CASE("Test jump point search path finding", "[JumpPointSearch]")
{
    Lines obstacles{};
    obstacles.push_back(Line(scaled(Vec2d(0, 0)), scaled(Vec2d(50, 50))));
    obstacles.push_back(Line(scaled(Vec2d(0, 100)), scaled(Vec2d(50, 50))));
    obstacles.push_back(Line(scaled(Vec2d(0, 0)), scaled(Vec2d(100, 0))));
    obstacles.push_back(Line(scaled(Vec2d(0, 100)), scaled(Vec2d(100, 100))));
    obstacles.push_back(Line(scaled(Vec2d(25, -25)), scaled(Vec2d(25, 125))));

    JPSPathFinder jps;
    jps.add_obstacles(obstacles);

    Polyline path = jps.find_path(scaled(Vec2d(5, 50)), scaled(Vec2d(100, 50)));
    path = jps.find_path(scaled(Vec2d(5, 50)), scaled(Vec2d(150, 50)));
    path = jps.find_path(scaled(Vec2d(5, 50)), scaled(Vec2d(25, 15)));
    path = jps.find_path(scaled(Vec2d(25, 25)), scaled(Vec2d(125, 125)));

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
