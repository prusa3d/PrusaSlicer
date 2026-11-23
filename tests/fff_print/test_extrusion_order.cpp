#include <catch2/catch_test_macros.hpp>

#include "libslic3r/GCode/ExtrusionOrder.hpp"

using namespace Slic3r;

TEST_CASE("Island order follows the nozzle relative to the instance", "[GCode]")
{
    LayerSlice slice;
    for (int x : {0, 10, 20}) {
        auto &island = slice.islands.emplace_back();
        island.boundary.contour.points = {scaled(Vec2d(x, 0)), scaled(Vec2d(x + 1, 0)), scaled(Vec2d(x, 1))};
    }

    const auto order = [&](const Point &offset, const Point &local_nozzle) {
        const Point bed_nozzle = local_nozzle + offset;
        std::vector<const LayerIsland *> result;
        for (const LayerIsland &island : GCode::ExtrusionOrder::get_ordered_islands(slice, bed_nozzle, offset))
            result.push_back(&island);
        return result;
    };

    const std::vector<const LayerIsland *> left_to_right{&slice.islands[0], &slice.islands[1], &slice.islands[2]};
    const std::vector<const LayerIsland *> right_to_left(left_to_right.rbegin(), left_to_right.rend());
    for (const Point &offset : {Point{0, 0}, Point{scaled(Vec2d(120, 75))}}) {
        REQUIRE(order(offset, scaled(Vec2d(-1, 0))) == left_to_right);
        REQUIRE(order(offset, scaled(Vec2d(21, 0))) == right_to_left);
    }
}
