#include <catch2/catch_test_macros.hpp>
#include "Slic3r/Domain/Preset/PresetTree.hpp"

using namespace Slic3r::Domain::Preset;


TEST_CASE("Preset name utils")
{
    REQUIRE(derive_name("X@C", "A@B") == "X@C@B");
    REQUIRE(derive_name("X", "A@B") == "X@B");
    REQUIRE(derive_name("X", "A") == "X");
    REQUIRE(derive_name("", "A") == "A");
    REQUIRE(derive_name("A", "") == "A");
    REQUIRE(derive_name("X@B", "A@B") == "X@B");
    REQUIRE(derive_name("X@B@D", derive_name("A@B", "B@C")) == "X@D@B@C");
}