#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <libslic3r/InfillAboveBridges.hpp>

using namespace Slic3r;
using Catch::Approx;

const ExPolygon square{
    Point::new_scale(0, 0),
    Point::new_scale(10, 0),
    Point::new_scale(10, 10),
    Point::new_scale(0, 10)
};

ExPolygon translate(const ExPolygon &polygon, const Point &offset) {
    ExPolygons result{polygon};
    translate(result, offset);
    return result.front();
}

constexpr bool debug_files{false};

void draw_surfaces(const PrepareInfill::SurfaceRefsByRegion &surfaces, std::string_view file_name) {
    using PrepareInfill::SurfaceCollectionRef;

    SurfaceCollection to_display;
    for (const SurfaceCollectionRef &surface_collection : surfaces) {
        to_display.append(surface_collection.get());
    }
    to_display.export_to_svg(file_name.data(), false);
}

TEST_CASE("Separate infill above bridges", "[PrepareInfill]") {
    ExPolygons layer_0_region_0_bridge{
        square
    };
    ExPolygons layer_0_region_0_internal{
        translate(square, Point::new_scale(10, 0))
    };
    ExPolygons layer_0_region_1_internal{
        translate(square, Point::new_scale(0, 10))
    };
    ExPolygons layer_0_region_1_bridge{
        translate(square, Point::new_scale(10, 10))
    };
    SurfaceCollection layer_0_region_0;
    layer_0_region_0.append(layer_0_region_0_bridge, stBottomBridge);
    layer_0_region_0.append(layer_0_region_0_internal, stInternal);
    SurfaceCollection layer_0_region_1;
    layer_0_region_1.append(layer_0_region_1_bridge, stBottomBridge);
    layer_0_region_1.append(layer_0_region_1_internal, stInternal);

    PrepareInfill::SurfaceRefsByRegion layer_0{layer_0_region_0, layer_0_region_1};

    ExPolygons layer_1_region_0_solid{
        translate(square, Point::new_scale(5, 5))
    };
    SurfaceCollection layer_1_region_0;
    layer_1_region_0.append(layer_1_region_0_solid, stInternalSolid);
    PrepareInfill::SurfaceRefsByRegion layer_1{layer_1_region_0};

    if constexpr (debug_files) {
        draw_surfaces(layer_0, "layer_0.svg");
    }

    PrepareInfill::separate_infill_above_bridges({layer_0, layer_1}, 0);

    if constexpr (debug_files) {
        draw_surfaces(layer_1, "layer_1.svg");
    }

    const Surfaces &result{layer_1.front().get().surfaces};
    REQUIRE(result.size() == 4);
    const double expected_area{scale_(5.0) * scale_(5.0)};
    CHECK(result[0].expolygon.contour.area() == Approx(expected_area));
    CHECK(result[0].surface_type == stInternalSolid);
    CHECK(result[1].expolygon.contour.area() == Approx(expected_area));
    CHECK(result[1].surface_type == stInternalSolid);
    CHECK(result[2].expolygon.contour.area() == Approx(expected_area));
    CHECK(result[2].surface_type == stSolidOverBridge);
    CHECK(result[3].expolygon.contour.area() == Approx(expected_area));
    CHECK(result[3].surface_type == stSolidOverBridge);
}
