#include <catch2/catch_test_macros.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

SCENARIO("PrintObject: Perimeter generation", "[PrintObject]") {
    GIVEN("20mm cube and default config") {
        WHEN("make_perimeters() is called")  {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, { { "fill_density", 0 } });
			const PrintObject &object = *print.objects().front();
			THEN("67 layers exist in the model") {
                REQUIRE(object.layers().size() == 66);
            }
            THEN("Every layer in region 0 has 1 island of perimeters") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters().size() == 1);
            }
            THEN("Every layer in region 0 has 3 paths in its perimeters list.") {
                for (const Layer *layer : object.layers())
                    REQUIRE(layer->regions().front()->perimeters().items_count() == 3);
            }
        }
    }
}

SCENARIO("Print: Skirt generation", "[Print]") {
    GIVEN("20mm cube and default config") {
        WHEN("Skirts is set to 2 loops")  {
            Slic3r::Print print;
            Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
            	{ "skirt_height", 	1 },
        		{ "skirt_distance", 1 },
        		{ "skirts", 		2 }
            });
            THEN("Skirt Extrusion collection has 2 loops in it") {
                REQUIRE(print.skirt().items_count() == 2);
                REQUIRE(print.skirt().flatten().entities.size() == 2);
            }
        }
    }
}

SCENARIO("Print: Changing number of solid surfaces does not cause all surfaces to become internal.", "[Print]") {
    GIVEN("sliced 20mm cube and config with top_solid_surfaces = 2 and bottom_solid_surfaces = 1") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
		config.set_deserialize_strict({
			{ "top_solid_layers",		2 },
			{ "bottom_solid_layers",	1 },
			{ "layer_height",			0.25 }, // get a known number of layers
			{ "first_layer_height",		0.25 }
			});
        Slic3r::Print print;
        Slic3r::Model model;
        Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
        // Precondition: Ensure that the model has 2 solid top layers (39, 38)
        // and one solid bottom layer (0).
		auto test_is_solid_infill = [&print](size_t obj_id, size_t layer_id) {
		    const Layer &layer = *print.objects()[obj_id]->get_layer((int)layer_id);
		    // iterate over all of the regions in the layer
		    for (const LayerRegion *region : layer.regions()) {
		        // for each region, iterate over the fill surfaces
		        for (const Surface &surface : region->fill_surfaces())
		            CHECK(surface.is_solid());
		    }
		};
        print.process();
        test_is_solid_infill(0,  0); // should be solid
        test_is_solid_infill(0, 79); // should be solid
        test_is_solid_infill(0, 78); // should be solid
        WHEN("Model is re-sliced with top_solid_layers == 3") {
			config.set("top_solid_layers", 3);
			print.apply(model, config);
            print.process();
            THEN("Print object does not have 0 solid bottom layers.") {
                test_is_solid_infill(0, 0);
            }
            AND_THEN("Print object has 3 top solid layers") {
                test_is_solid_infill(0, 79);
                test_is_solid_infill(0, 78);
                test_is_solid_infill(0, 77);
            }
        }
    }
}

SCENARIO("Print: Brim generation", "[Print]") {
    GIVEN("20mm cube and default config, 1mm first layer width") {
        WHEN("Brim is set to 3mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
	        	{ "first_layer_extrusion_width", 	1 },
	        	{ "brim_width", 					3 }
	        });
            THEN("Brim Extrusion collection has 3 loops in it") {
                REQUIRE(print.brim().items_count() == 3);
            }
        }
        WHEN("Brim is set to 6mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
	        	{ "first_layer_extrusion_width", 	1 },
	        	{ "brim_width", 					6 }
	        });
            THEN("Brim Extrusion collection has 6 loops in it") {
                REQUIRE(print.brim().items_count() == 6);
            }
        }
        WHEN("Brim is set to 6mm, extrusion width 0.5mm")  {
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, {
	        	{ "first_layer_extrusion_width", 	1 },
	        	{ "brim_width", 					6 },
	        	{ "first_layer_extrusion_width", 	0.5 }
	        });
			print.process();
            THEN("Brim Extrusion collection has 12 loops in it") {
                REQUIRE(print.brim().items_count() == 14);
            }
        }
    }
}

SCENARIO("Ported from Perl", "[Print]") {
    GIVEN("20mm cube") {
        WHEN("Print center is set to 100x100 (test framework default)")  {
            auto config = Slic3r::DynamicPrintConfig::full_print_config();
            std::string gcode = Slic3r::Test::slice({ TestMesh::cube_20x20x20 }, config);
            GCodeReader parser;
            Points      extrusion_points;
            parser.parse_buffer(gcode, [&extrusion_points](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
            {
                if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0)
                    extrusion_points.emplace_back(line.new_XY_scaled(self));
            });
            Vec2d center = unscaled<double>(BoundingBox(extrusion_points).center());
            THEN("print is centered around print_center") {
                REQUIRE(is_approx(center.x(), 100.));
                REQUIRE(is_approx(center.y(), 100.));
            }
        }
    }
    GIVEN("Model with multiple objects") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "nozzle_diameter", { 0.4, 0.4, 0.4, 0.4 } }
        });
        Print print;
        Model model;
        Slic3r::Test::init_print({ TestMesh::cube_20x20x20 }, print, model, config);
        
        // User sets a per-region option, also testing a deep copy of Model.
        Model model2(model);
        model2.objects.front()->config.set_deserialize_strict("fill_density", "100%");
        WHEN("fill_density overridden") {
            print.apply(model2, config);
            THEN("region config inherits model object config") {
                REQUIRE(print.get_print_region(0).config().fill_density == 100);
            }
        }

        model2.objects.front()->config.erase("fill_density");
        WHEN("fill_density resetted") {
            print.apply(model2, config);
            THEN("region config is resetted") {
                REQUIRE(print.get_print_region(0).config().fill_density == 20);
            }
        }

        WHEN("extruder is assigned") {
            model2.objects.front()->config.set("extruder", 3);
            model2.objects.front()->config.set("perimeter_extruder", 2);
            print.apply(model2, config);
            THEN("extruder setting is correctly expanded") {
                REQUIRE(print.get_print_region(0).config().infill_extruder == 3);
            }
            THEN("extruder setting does not override explicitely specified extruders") {
                REQUIRE(print.get_print_region(0).config().perimeter_extruder == 2);
            }
        }
    }
}
