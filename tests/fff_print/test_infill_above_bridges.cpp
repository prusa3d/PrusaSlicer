#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/GCode/ExtrusionProcessor.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;
using Catch::Approx;

namespace AABBTreeLines = Slic3r::Biz::Algorithms::AABBTreeLines;

const ExPolygon
    bridge{scaled(Vec2d{0, 0}), scaled(Vec2d{10, 0}), scaled(Vec2d{10, 10}), scaled(Vec2d{0, 10})};

static Polyline horizontal_line(const double y, const double x_start, const double x_end)
{
    return Polyline{scaled(Vec2d{x_start, y}), scaled(Vec2d{x_end, y})};
}

static bool is_limited_over_bridge(const ExtrusionPath& path)
{
    return path.attributes().speed_limits.has(ExtrusionSpeedLimit::OverBridge);
}

static ExtrusionEntityCollection
split_solid_infill_by_bridges(const ExtrusionEntityCollection& fills, const ExPolygons& bridges)
{
    const AABBTreeLines::LinesDistancer<Line> bridges_boundary{
        Algorithms::ExPolygon::to_lines(bridges)
    };
    return ExtrusionProcessor::split_extrusions_by_speed_limit_region(
        fills,
        bridges_boundary,
        ExtrusionSpeedLimit::OverBridge,
        [](const ExtrusionRole role) { return role == ExtrusionRole::SolidInfill; }
    );
}

const ExtrusionFlow infill_flow{0.1, 0.45f, 0.2f};

TEST_CASE("Split solid infill above bridges", "[SpeedLimits]")
{
    ExtrusionEntityCollection fills;
    fills.append(
        ExtrusionPath{
            horizontal_line(5, -5, 15),
            ExtrusionAttributes{ExtrusionRole::SolidInfill, infill_flow}
        }
    );
    fills.append(
        ExtrusionPathOriented{
            horizontal_line(2, -5, 15),
            ExtrusionAttributes{ExtrusionRole::SolidInfill, infill_flow}
        }
    );
    fills.append(
        ExtrusionPath{
            horizontal_line(8, -5, 15),
            ExtrusionAttributes{ExtrusionRole::TopSolidInfill, infill_flow}
        }
    );
    fills.append(
        ExtrusionPath{
            horizontal_line(5, 20, 30),
            ExtrusionAttributes{ExtrusionRole::SolidInfill, infill_flow}
        }
    );
    fills.append(
        ExtrusionPath{
            horizontal_line(5, 2, 8),
            ExtrusionAttributes{ExtrusionRole::SolidInfill, infill_flow}
        }
    );
    // The fills of a surface are stored in a nested collection.
    ExtrusionEntityCollection surface_fills;
    surface_fills.no_sort = true;
    surface_fills.append(
        ExtrusionPath{
            horizontal_line(5, -5, 15),
            ExtrusionAttributes{ExtrusionRole::SolidInfill, infill_flow}
        }
    );
    fills.append(std::move(surface_fills));

    const ExtrusionEntityCollection result{split_solid_infill_by_bridges(fills, {bridge})};

    // The top level structure has to be preserved, layer islands refer to it by indices.
    REQUIRE(result.entities.size() == fills.entities.size());

    SECTION("Solid infill crossing the bridge is split, only the part above the bridge is limited")
    {
        const ExtrusionMultiPath* multipath{
            dynamic_cast<const ExtrusionMultiPath*>(result.entities[0])
        };
        REQUIRE(multipath != nullptr);
        CHECK(multipath->can_reverse());
        REQUIRE(multipath->paths.size() == 3);
        CHECK(!is_limited_over_bridge(multipath->paths[0]));
        CHECK(is_limited_over_bridge(multipath->paths[1]));
        CHECK(!is_limited_over_bridge(multipath->paths[2]));
        CHECK(multipath->paths[1].length() == Approx(scale_(10.)));
    }

    SECTION("Parts of an oriented path stay oriented")
    {
        const ExtrusionMultiPathOriented* multipath{
            dynamic_cast<const ExtrusionMultiPathOriented*>(result.entities[1])
        };
        REQUIRE(multipath != nullptr);
        CHECK(!multipath->can_reverse());
        REQUIRE(multipath->paths.size() == 3);
        CHECK(is_limited_over_bridge(multipath->paths[1]));
    }

    SECTION("Other roles are not split")
    {
        const ExtrusionPath* path{dynamic_cast<const ExtrusionPath*>(result.entities[2])};
        REQUIRE(path != nullptr);
        CHECK(!is_limited_over_bridge(*path));
    }

    SECTION("Solid infill outside of the bridge is not limited")
    {
        const ExtrusionPath* path{dynamic_cast<const ExtrusionPath*>(result.entities[3])};
        REQUIRE(path != nullptr);
        CHECK(!is_limited_over_bridge(*path));
    }

    SECTION("Solid infill inside of the bridge is limited without splitting")
    {
        const ExtrusionPath* path{dynamic_cast<const ExtrusionPath*>(result.entities[4])};
        REQUIRE(path != nullptr);
        CHECK(is_limited_over_bridge(*path));
    }

    SECTION("Nested collections are preserved")
    {
        const ExtrusionEntityCollection* collection{
            dynamic_cast<const ExtrusionEntityCollection*>(result.entities[5])
        };
        REQUIRE(collection != nullptr);
        CHECK(collection->no_sort);
        REQUIRE(collection->entities.size() == 1);
        const ExtrusionMultiPath* multipath{
            dynamic_cast<const ExtrusionMultiPath*>(collection->entities.front())
        };
        REQUIRE(multipath != nullptr);
        REQUIRE(multipath->paths.size() == 3);
        CHECK(is_limited_over_bridge(multipath->paths[1]));
    }
}

TEST_CASE("Split solid infill above several bridges", "[SpeedLimits]")
{
    const ExPolygon second_bridge{
        scaled(Vec2d{20, 0}),
        scaled(Vec2d{30, 0}),
        scaled(Vec2d{30, 10}),
        scaled(Vec2d{20, 10})
    };
    ExtrusionEntityCollection fills;
    fills.append(
        ExtrusionPath{
            horizontal_line(5, -5, 35),
            ExtrusionAttributes{ExtrusionRole::SolidInfill, infill_flow}
        }
    );

    const ExtrusionEntityCollection result{
        split_solid_infill_by_bridges(fills, {bridge, second_bridge})
    };

    const ExtrusionMultiPath* multipath{
        dynamic_cast<const ExtrusionMultiPath*>(result.entities.front())
    };
    REQUIRE(multipath != nullptr);
    REQUIRE(multipath->paths.size() == 5);
    CHECK(!is_limited_over_bridge(multipath->paths[0]));
    CHECK(is_limited_over_bridge(multipath->paths[1]));
    CHECK(!is_limited_over_bridge(multipath->paths[2]));
    CHECK(is_limited_over_bridge(multipath->paths[3]));
    CHECK(!is_limited_over_bridge(multipath->paths[4]));
    CHECK(multipath->paths[1].length() == Approx(scaled<double>(10.)));
    CHECK(multipath->paths[3].length() == Approx(scaled<double>(10.)));
}
