#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/SVG.hpp"
#include <catch2/catch.hpp>
#include <libslic3r/LayerRegion.hpp>

using namespace Slic3r;
using namespace Slic3r::Algorithm;

constexpr bool export_svgs = false;

ExPolygon rectangle(const Point& origin, const int width, const int height) {
    return {
        origin,
        origin + Point{width, 0},
        origin + Point{width, height},
        origin + Point{0, height},
    };
}

struct LayerRegionFixture {
    Surfaces surfaces{
        Surface{
            stBottomBridge,
            rectangle({scaled(-1.0), scaled(0.0)}, scaled(1.0), scaled(1.0))
        },
        Surface{
            stBottomBridge,
            rectangle({scaled(0.0), scaled(0.0)}, scaled(1.0), scaled(1.0))
        },
        Surface{
            stBottomBridge,
            rectangle({scaled(-3.0), scaled(0.0)}, scaled(1.0), scaled(1.0))
        }
    };

    ExPolygons shells{{
       rectangle({scaled(-1.0), scaled(1.0)}, scaled(3.0), scaled(1.0))
    }};
    ExPolygons sparse {{
       rectangle({scaled(-2.0), scaled(-1.0)}, scaled(1.0), scaled(3.0))
    }};

    const float scaled_spacing{scaled(0.3)};

    static constexpr const float    expansion_step          = scaled<float>(0.1);
    static constexpr const size_t   max_nr_expansion_steps  = 5;
    const float closing_radius = 0.55f * 0.65f * 1.05f * scaled_spacing;
    const int shells_expansion_depth = scaled(0.6);
    const RegionExpansionParameters expansion_params_into_solid_infill = RegionExpansionParameters::build(
        shells_expansion_depth,
        expansion_step,
        max_nr_expansion_steps
    );
    const int sparse_expansion_depth = scaled(0.3);
    const RegionExpansionParameters expansion_params_into_sparse_infill = RegionExpansionParameters::build(
        sparse_expansion_depth,
        expansion_step,
        max_nr_expansion_steps
    );

    std::vector<ExpansionZone> expansion_zones{
        ExpansionZone{
            std::move(shells),
            expansion_params_into_solid_infill,
        },
        ExpansionZone{
            std::move(sparse),
            expansion_params_into_sparse_infill,
        }
    };
};

TEST_CASE_METHOD(LayerRegionFixture, "test the surface expansion", "[LayerRegion]") {
    const double custom_angle{1.234f};

    const Surfaces result{expand_merge_surfaces(
        surfaces, stBottomBridge,
        expansion_zones,
        closing_radius,
        custom_angle
    )};

    if constexpr (export_svgs) {
        SVG svg("general_expansion.svg", BoundingBox{
            Point{scaled(-3.0), scaled(-1.0)},
            Point{scaled(2.0), scaled(2.0)}
        });

        svg.draw(surfaces, "blue");
        svg.draw(expansion_zones[0].expolygons, "green");
        svg.draw(expansion_zones[1].expolygons, "red");
        svg.draw_outline(result, "black", "", scale_(0.01));
    }

    REQUIRE(result.size() == 2);
    CHECK(result.at(0).bridge_angle == Approx(custom_angle));
    CHECK(result.at(1).bridge_angle == Approx(custom_angle));
    CHECK(result.at(0).expolygon.contour.size() == 22);
    CHECK(result.at(1).expolygon.contour.size() == 14);

    // These lines in the polygons should correspond to the expansion depth.
    CHECK(result.at(0).expolygon.contour.lines().at(2).length() == shells_expansion_depth);
    CHECK(result.at(1).expolygon.contour.lines().at(7).length() == sparse_expansion_depth);
    CHECK(result.at(1).expolygon.contour.lines().at(11).length() == sparse_expansion_depth);

    CHECK(intersection_ex({result.at(0).expolygon}, expansion_zones[0].expolygons).size() == 0);
    CHECK(intersection_ex({result.at(0).expolygon}, expansion_zones[1].expolygons).size() == 0);
    CHECK(intersection_ex({result.at(1).expolygon}, expansion_zones[0].expolygons).size() == 0);
    CHECK(intersection_ex({result.at(1).expolygon}, expansion_zones[1].expolygons).size() == 0);
}

TEST_CASE_METHOD(LayerRegionFixture, "test the bridge expansion with the bridge angle detection", "[LayerRegion]") {
    Surfaces result{expand_bridges_detect_orientations(
        surfaces,
        expansion_zones,
        closing_radius
    )};

    if constexpr (export_svgs) {
        SVG svg("bridge_expansion.svg", BoundingBox{
            Point{scaled(-3.0), scaled(-1.0)},
            Point{scaled(2.0), scaled(2.0)}
        });

        svg.draw(surfaces, "blue");
        svg.draw(expansion_zones[0].expolygons, "green");
        svg.draw(expansion_zones[1].expolygons, "red");
        svg.draw_outline(result, "black", "", scale_(0.01));
    }

    REQUIRE(result.size() == 2);
    CHECK(std::fmod(result.at(1).bridge_angle, M_PI) == Approx(0.0));
    CHECK(std::fmod(result.at(1).bridge_angle, M_PI) == Approx(0.0));
    CHECK(result.at(0).expolygon.contour.size() == 22);
    CHECK(result.at(1).expolygon.contour.size() == 14);

    // These lines in the polygons should correspond to the expansion depth.
    CHECK(result.at(0).expolygon.contour.lines().at(2).length() == shells_expansion_depth);
    CHECK(result.at(1).expolygon.contour.lines().at(7).length() == sparse_expansion_depth);
    CHECK(result.at(1).expolygon.contour.lines().at(11).length() == sparse_expansion_depth);

    CHECK(intersection_ex({result.at(0).expolygon}, expansion_zones[0].expolygons).size() == 0);
    CHECK(intersection_ex({result.at(0).expolygon}, expansion_zones[1].expolygons).size() == 0);
    CHECK(intersection_ex({result.at(1).expolygon}, expansion_zones[0].expolygons).size() == 0);
    CHECK(intersection_ex({result.at(1).expolygon}, expansion_zones[1].expolygons).size() == 0);
}
