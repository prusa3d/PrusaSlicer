#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>
#include <sstream>

#include "test_data.hpp" // get access to init_print, etc

#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/libslic3r.h"

using namespace Slic3r::Test;
using namespace Slic3r;
using namespace Catch;

SCENARIO("Extrusion width specifics", "[Flow]") {

    auto test = [](const DynamicPrintConfig &config) {
        Slic3r::GCodeReader parser;
        const double        layer_height = config.opt_float("layer_height");
        std::vector<double> E_per_mm_bottom;
        parser.parse_buffer(Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config),
            [&E_per_mm_bottom, layer_height] (Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
        { 
            if (self.z() == Approx(layer_height).margin(0.01)) { // only consider first layer
                if (line.extruding(self) && line.dist_XY(self) > 0)
                    E_per_mm_bottom.emplace_back(line.dist_E(self) / line.dist_XY(self));
            }
        });
        THEN("First layer width applies to everything on first layer.") {
            REQUIRE(E_per_mm_bottom.size() > 0);
            const double E_per_mm_avg = std::accumulate(E_per_mm_bottom.cbegin(), E_per_mm_bottom.cend(), 0.0) / static_cast<double>(E_per_mm_bottom.size());
            bool pass = (std::count_if(E_per_mm_bottom.cbegin(), E_per_mm_bottom.cend(), [E_per_mm_avg] (const double& v) { return v == Approx(E_per_mm_avg); }) == 0);
            REQUIRE(pass);
        }
        THEN("First layer width does not apply to upper layer.") {
        }
    };
    GIVEN("A config with a skirt, brim, some fill density, 3 perimeters, and 1 bottom solid layer") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "skirts",                         1 },
            { "brim_width",                     2 },
            { "perimeters",                     3 },
            { "fill_density",                   "40%" },
            { "first_layer_height",             0.3 },
            { "first_layer_extrusion_width",    "2" },
        });
        WHEN("Slicing a 20mm cube") {
            test(config);
        }
    }
    GIVEN("A config with more options and a 20mm cube ") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "skirts",                         1 },
            { "brim_width",                     2 },
            { "perimeters",                     3 },
            { "fill_density",                   "40%" },
            { "layer_height",                   "0.35" },
            { "first_layer_height",             "0.35" },
            { "bottom_solid_layers",            1 },
            { "first_layer_extrusion_width",    "2" },
            { "filament_diameter",              "3" },
            { "nozzle_diameter",                "0.5" }
        });
        WHEN("Slicing a 20mm cube") {
            test(config);            
        }
    }
}

SCENARIO(" Bridge flow specifics.", "[Flow]") {
    auto config = DynamicPrintConfig::full_print_config_with({
        { "bridge_speed",           99 },
        { "bridge_flow_ratio",      1 },
        // to prevent speeds from being altered
        { "cooling",                "0" },
        // to prevent speeds from being altered
        { "first_layer_speed",      "100%" }
    });

    auto test = [](const DynamicPrintConfig &config) {
        GCodeReader         parser;
        const double        bridge_speed = config.opt_float("bridge_speed") * 60.;
        std::vector<double> E_per_mm;
        parser.parse_buffer(Slic3r::Test::slice({ Slic3r::Test::TestMesh::overhang }, config), 
            [&E_per_mm, bridge_speed](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
            if (line.extruding(self) && line.dist_XY(self) > 0) {
                if (is_approx<double>(line.new_F(self), bridge_speed))
                    E_per_mm.emplace_back(line.dist_E(self) / line.dist_XY(self));
            }
        });
        const double nozzle_dmr                 = config.opt<ConfigOptionFloats>("nozzle_diameter")->get_at(0);
        const double filament_dmr               = config.opt<ConfigOptionFloats>("filament_diameter")->get_at(0);
        const double bridge_mm_per_mm           = sqr(nozzle_dmr / filament_dmr) * config.opt_float("bridge_flow_ratio");
        size_t num_errors = std::count_if(E_per_mm.begin(), E_per_mm.end(), 
            [bridge_mm_per_mm](double v){ return std::abs(v - bridge_mm_per_mm) > 0.01; });
        return num_errors == 0;
    };

    GIVEN("A default config with no cooling and a fixed bridge speed, flow ratio and an overhang mesh.") {
        WHEN("bridge_flow_ratio is set to 0.5 and extrusion width to default") {
            config.set_deserialize_strict({ { "bridge_flow_ratio", 0.5}, { "extrusion_width", "0" } });
            THEN("Output flow is as expected.") {
                REQUIRE(test(config));
            }
        }
        WHEN("bridge_flow_ratio is set to 2.0 and extrusion width to default") {
            config.set_deserialize_strict({ { "bridge_flow_ratio", 2.0}, { "extrusion_width", "0" } });
            THEN("Output flow is as expected.") {
                REQUIRE(test(config));
            }
        }
        WHEN("bridge_flow_ratio is set to 0.5 and extrusion_width to 0.4") {
            config.set_deserialize_strict({ { "bridge_flow_ratio", 0.5}, { "extrusion_width", 0.4 } });
            THEN("Output flow is as expected.") {
                REQUIRE(test(config));
            }
        }
        WHEN("bridge_flow_ratio is set to 1.0 and extrusion_width to 0.4") {
            config.set_deserialize_strict({ { "bridge_flow_ratio", 1.0}, { "extrusion_width", 0.4 } });
            THEN("Output flow is as expected.") {
                REQUIRE(test(config));
            }
        }
        WHEN("bridge_flow_ratio is set to 2 and extrusion_width to 0.4") {
            config.set_deserialize_strict({ { "bridge_flow_ratio", 2.}, { "extrusion_width", 0.4 } });
            THEN("Output flow is as expected.") {
                REQUIRE(test(config));
            }
        }
    }
    GIVEN("A default config with no cooling and a fixed bridge speed, flow ratio, fixed extrusion width of 0.4mm and an overhang mesh.") {
        WHEN("bridge_flow_ratio is set to 1.0") {
            THEN("Output flow is as expected.") {
            }
        }
        WHEN("bridge_flow_ratio is set to 0.5") {
            THEN("Output flow is as expected.") {
            }
        }
        WHEN("bridge_flow_ratio is set to 2.0") {
            THEN("Output flow is as expected.") {
            }
        }
    }
}

/// Test the expected behavior for auto-width, 
/// spacing, etc
SCENARIO("Flow: Flow math for non-bridges", "[Flow]") {
    GIVEN("Nozzle Diameter of 0.4, a desired width of 1mm and layer height of 0.5") {
        ConfigOptionFloatOrPercent	width(1.0, false);
        float nozzle_diameter	= 0.4f;
        float layer_height		= 0.4f;

        // Spacing for non-bridges is has some overlap
        THEN("External perimeter flow has spacing fixed to 1.125 * nozzle_diameter") {
            auto flow = Flow::new_from_config_width(frExternalPerimeter, ConfigOptionFloatOrPercent(0, false), nozzle_diameter, layer_height);
            REQUIRE(flow.spacing() == Approx(1.125 * nozzle_diameter - layer_height * (1.0 - PI / 4.0)));
        }

        THEN("Internal perimeter flow has spacing fixed to 1.125 * nozzle_diameter") {
            auto flow = Flow::new_from_config_width(frPerimeter, ConfigOptionFloatOrPercent(0, false), nozzle_diameter, layer_height);
            REQUIRE(flow.spacing() == Approx(1.125 *nozzle_diameter - layer_height * (1.0 - PI / 4.0)));
        }
        THEN("Spacing for supplied width is 0.8927f") {
            auto flow = Flow::new_from_config_width(frExternalPerimeter, width, nozzle_diameter, layer_height);
            REQUIRE(flow.spacing() == Approx(width.value - layer_height * (1.0 - PI / 4.0)));
            flow = Flow::new_from_config_width(frPerimeter, width, nozzle_diameter, layer_height);
            REQUIRE(flow.spacing() == Approx(width.value - layer_height * (1.0 - PI / 4.0)));
        }
    }
    /// Check the min/max
    GIVEN("Nozzle Diameter of 0.25") {
        float nozzle_diameter	= 0.25f;
        float layer_height		= 0.5f;
        WHEN("layer height is set to 0.2") {
            layer_height = 0.15f;
            THEN("Max width is set.") {
                auto flow = Flow::new_from_config_width(frPerimeter, ConfigOptionFloatOrPercent(0, false), nozzle_diameter, layer_height);
                REQUIRE(flow.width() == Approx(1.125 * nozzle_diameter));
            }
        }
        WHEN("Layer height is set to 0.25") {
            layer_height = 0.25f;
            THEN("Min width is set.") {
                auto flow = Flow::new_from_config_width(frPerimeter, ConfigOptionFloatOrPercent(0, false), nozzle_diameter, layer_height);
                REQUIRE(flow.width() == Approx(1.125 * nozzle_diameter));
            }
        }
    }

#if 0
    /// Check for an edge case in the maths where the spacing could be 0; original
    /// math is 0.99. Slic3r issue #4654
    GIVEN("Input spacing of 0.414159 and a total width of 2") {
        double in_spacing = 0.414159;
        double total_width = 2.0;
        auto flow = Flow::new_from_spacing(1.0, 0.4, 0.3);
        WHEN("solid_spacing() is called") {
            double result = flow.solid_spacing(total_width, in_spacing);
            THEN("Yielded spacing is greater than 0") {
                REQUIRE(result > 0);
            }
        }
    }
#endif    

}

/// Spacing, width calculation for bridge extrusions
SCENARIO("Flow: Flow math for bridges", "[Flow]") {
    GIVEN("Nozzle Diameter of 0.4, a desired width of 1mm and layer height of 0.5") {
		float nozzle_diameter	= 0.4f;
		float bridge_flow		= 1.0f;
        WHEN("Flow role is frExternalPerimeter") {
            auto flow = Flow::bridging_flow(nozzle_diameter * sqrt(bridge_flow), nozzle_diameter);
            THEN("Bridge width is same as nozzle diameter") {
                REQUIRE(flow.width() == Approx(nozzle_diameter));
            }
            THEN("Bridge spacing is same as nozzle diameter + BRIDGE_EXTRA_SPACING") {
                REQUIRE(flow.spacing() == Approx(nozzle_diameter + BRIDGE_EXTRA_SPACING));
            }
        }
    }
}
