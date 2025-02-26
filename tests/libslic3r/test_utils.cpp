#include <catch2/catch_test_macros.hpp>

#include "libslic3r/libslic3r.h"

SCENARIO("Test fast_round_up()") {
    using namespace Slic3r;

    THEN("fast_round_up<int>(1.5) is 2") {
        REQUIRE(fast_round_up<int>(1.5) == 2);
    }
    THEN("fast_round_up<int>(1.499999999999999) is 1") {
        REQUIRE(fast_round_up<int>(1.499999999999999) == 1);
    }
    THEN("fast_round_up<int>(0.5) is 1") {
        REQUIRE(fast_round_up<int>(0.5) == 1);
    }
    THEN("fast_round_up<int>(0.49999999999999994) is 0") {
        REQUIRE(fast_round_up<int>(0.49999999999999994) == 0);
    }
    THEN("fast_round_up<int>(-0.5) is 0") {
        REQUIRE(fast_round_up<int>(-0.5) == 0);
    }
    THEN("fast_round_up<int>(-0.51) is -1") {
        REQUIRE(fast_round_up<int>(-0.51) == -1);
    }
    THEN("fast_round_up<int>(-0.51) is -1") {
        REQUIRE(fast_round_up<int>(-0.51) == -1);
    }
    THEN("fast_round_up<int>(-1.5) is -1") {
        REQUIRE(fast_round_up<int>(-1.5) == -1);
    }
    THEN("fast_round_up<int>(-1.51) is -2") {
        REQUIRE(fast_round_up<int>(-1.51) == -2);
    }
}
