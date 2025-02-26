#include <catch2/catch_test_macros.hpp>

#include "test_data.hpp"

using namespace Slic3r;

SCENARIO("Avoid crossing perimeters", "[AvoidCrossingPerimeters]") {
	WHEN("Two 20mm cubes sliced") {
        std::string gcode = Slic3r::Test::slice(
    	    { Slic3r::Test::TestMesh::cube_20x20x20, Slic3r::Test::TestMesh::cube_20x20x20 },
            { { "avoid_crossing_perimeters", true } });
        THEN("gcode not empty") {
            REQUIRE(! gcode.empty());
        }
    }
}
