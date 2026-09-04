#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"
#include "libslic3r/Geometry.hpp"

#include <boost/algorithm/string.hpp>

#include "test_data.hpp" // get access to init_print, etc

#include <set>

using namespace Slic3r::Test;
using namespace Slic3r;
using namespace Catch;
using Biz::GCodeReader::GCodeReader;
using Domain::BrimType;
using Domain::FloatOrPercentage;
using Domain::Percentage;
using Domain::SupportMode;

/// Helper method to find the tool used for the brim (always the first extrusion)
static int get_brim_tool(const std::string &gcode)
{
    int brim_tool	= -1;
    int tool		= -1;
	GCodeReader parser;
    parser.parse_buffer(gcode, [&tool, &brim_tool] (GCodeReader &self, const GCodeReader::GCodeLine &line)
    {
        // if the command is a T command, set the the current tool
        if (boost::starts_with(line.cmd(), "T")) {
            tool = atoi(line.cmd().data() + 1);
        } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0 && brim_tool < 0) {
            brim_tool = tool;
        }
    });
    return brim_tool;
}

TEST_CASE("Skirt height is honored", "[Skirt]") {
    TestConfig config;
    config.print.items.opt("skirts").set(1);
    config.print.items.opt("skirt_height").set(5);
    config.print.items.opt("perimeters").set(0);
    config.print.items.opt("support_material_speed").set(99.0);
    // avoid altering speeds unexpectedly
    config.filament[0].items.opt("cooling").set(false);
    // avoid altering speeds unexpectedly
    config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});

	std::string gcode;
    SECTION("printing a single object") {
        gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
    }
    SECTION("printing multiple objects") {
        gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20, TestMesh::cube_20x20x20}, config);
    }

    std::map<double, bool> layers_with_skirt;
    double support_speed = config.print.items.opt("support_material_speed").get<double>() * MM_PER_MIN;
	GCodeReader parser;
    parser.parse_buffer(gcode, [&layers_with_skirt, &support_speed] (GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.extruding(self) && self.f() == Approx(support_speed)) {
            layers_with_skirt[self.z()] = 1;
        }
    });
    REQUIRE(layers_with_skirt.size() == (size_t)config.print.items.opt("skirt_height").get<int>());
}

TEST_CASE("Original Slic3r Skirt/Brim tests", "[SkirtBrim]") {
    GIVEN("A default configuration") {
	    TestConfig config{4};

        config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.3});
        config.print.items.opt("gcode_comments").set(true);
        config.print.items.opt("support_material_speed").set(99.0);
        config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});
        // remove noise from top/solid layers
        config.print.items.opt("top_solid_layers").set(0);
        config.print.items.opt("bottom_solid_layers").set(1);

        // avoid altering speeds unexpectedly
        for (auto& filament_settings : config.filament) {
            filament_settings.items.opt("cooling").set(false);
        }
        config.printer.items.opt("start_gcode").set("T[initial_tool]\n" );

        WHEN("Brim width is set to 5") {
            config.print.items.opt("perimeters").set(0);
            config.print.items.opt("skirts").set(0);
            config.print.items.opt("brim_type").set(BrimType::OuterOnly);
            config.print.items.opt("brim_width").set(5.0);
			THEN("Brim is generated") {
		        std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
                bool brim_generated = false;
                double support_speed = config.print.items.opt("support_material_speed").get<double>() * MM_PER_MIN;
			    GCodeReader parser;
                parser.parse_buffer(gcode, [&brim_generated, support_speed] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (self.z() == Approx(0.3) || line.new_Z(self) == Approx(0.3)) {
                        if (line.extruding(self) && self.f() == Approx(support_speed)) {
                            brim_generated = true;
                        }
                    }
                });
                REQUIRE(brim_generated);
            }
        }

        WHEN("Skirt area is smaller than the brim") {
            config.print.items.opt("skirts").set(1);
            config.print.items.opt("brim_type").set(BrimType::OuterOnly);
            config.print.items.opt("brim_width").set(10.0);
            THEN("Gcode generates") {
                REQUIRE(! Slic3r::Test::slice({TestMesh::cube_20x20x20}, config).empty());
            }
        }

        WHEN("Skirt height is 0 and skirts > 0") {
            config.print.items.opt("skirts").set(2);
            config.print.items.opt("skirt_height").set(0);
            THEN("Gcode generates") {
                REQUIRE(! Slic3r::Test::slice({TestMesh::cube_20x20x20}, config).empty());
            }
        }

#if 0
		// This is a real error! One shall print the brim with the external perimeter extruder!
        WHEN("Perimeter extruder = 2 and support extruders = 3") {
            THEN("Brim is printed with the extruder used for the perimeters of first object") {
				config.set_deserialize_strict({
					{ "skirts", 					0 },
					{ "brim_width", 				5 },
					{ "perimeter_extruder", 		2 },
					{ "support_material_extruder", 	3 },
					{ "infill_extruder", 			4 }
				});
		        std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
                int tool = get_brim_tool(gcode);
                REQUIRE(tool == config.opt_int("perimeter_extruder") - 1);
            }
        }
        WHEN("Perimeter extruder = 2, support extruders = 3, raft is enabled") {
            THEN("brim is printed with same extruder as skirt") {
				config.set_deserialize_strict({
					{ "skirts",						0 },
					{ "brim_width", 				5 },
					{ "perimeter_extruder", 		2 },
					{ "support_material_extruder", 	3 },
					{ "infill_extruder", 			4 },
					{ "raft_layers", 				1 }
				});
		        std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
                int tool = get_brim_tool(gcode);
                REQUIRE(tool == config.opt_int("support_material_extruder") - 1);
            }
        }
#endif

        WHEN("brim width to 1 with layer_width of 0.5") {
            config.print.items.opt("skirts").set(0);
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{0.5});
            config.print.items.opt("brim_type").set(BrimType::OuterOnly);
            config.print.items.opt("brim_width").set(1.0);
            THEN("2 brim lines") {
		        Slic3r::Print print;
		        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, config);
                REQUIRE(print.brim().entities.size() == 2);
            }
        }

#if 0
        WHEN("brim ears on a square") {
			config.set_deserialize_strict({
				{ "skirts",							0 },
				{ "first_layer_extrusion_width",	0.5 },
				{ "brim_width",						1 },
				{ "brim_ears",						1 },
				{ "brim_ears_max_angle",			91 }
			});
	        Slic3r::Print print;
	        Slic3r::Test::init_and_process_print({TestMesh::cube_20x20x20}, print, config);
            THEN("Four brim ears") {
                REQUIRE(print.brim().entities.size() == 4);
            }
        }

        WHEN("brim ears on a square but with a too small max angle") {
			config.set_deserialize_strict({
				{ "skirts",							0 },
				{ "first_layer_extrusion_width",	0.5 },
				{ "brim_width",						1 },
				{ "brim_ears",						1 },
				{ "brim_ears_max_angle",			89 }
				});
            THEN("no brim") {
		        Slic3r::Print print;
                Slic3r::Test::init_and_process_print({ TestMesh::cube_20x20x20 }, print, config);
                REQUIRE(print.brim().entities.size() == 0);
            }
        }
#endif

        WHEN("Object is plated with overhang support and a brim") {
            config.print.items.opt("layer_height").set(0.4);
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.4});
            config.print.items.opt("skirts").set(1);
            config.print.items.opt("skirt_distance").set(0.0);
            config.print.items.opt("perimeter_extruder").set(1 );
            config.print.items.opt("infill_extruder").set(3);

            config.print.items.opt("support_material_speed").set(99.0);
            config.print.items.opt("support_material_extruder").set(2);
            config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100.0}});

            for (auto& filament_settings : config.filament) {
                filament_settings.items.opt("cooling").set(false);
            }
            config.printer.items.opt("start_gcode").set("T[initial_tool]\n");

            THEN("overhang generates?") {
            	//FIXME does it make sense?
                REQUIRE(! Slic3r::Test::slice({TestMesh::overhang}, config).empty());
            }

            // config.set("support_material", true);      // to prevent speeds to be altered

#if 0
			// This test is not finished.
            THEN("skirt length is large enough to contain object with support") {
                CHECK(config.opt_bool("support_material")); // test is not valid if support material is off
				std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
                double support_speed = config.opt<Confi.items.optionFloat>("support_material_speed")->value * MM_PER_MIN;
				double skirt_length = 0.0;
				Points extrusion_points;
				int tool = -1;
				GCodeReader parser;
                parser.parse_buffer(gcode, [config, &extrusion_points, &tool, &skirt_length, support_speed] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    // std::cerr << line.cmd() << "\n";
					if (boost::starts_with(line.cmd(), "T")) {
						tool = atoi(line.cmd().data() + 1);
					} else if (self.z() == Approx(config.opt<Confi.items.optionFloat>("first_layer_height")->value)) {
                        // on first layer
						if (line.extruding(self) && line.dist_XY(self) > 0) {
                            float speed = ( self.f() > 0 ?  self.f() : line.new_F(self));
                            // std::cerr << "Tool " << tool << "\n";
                            if (speed == Approx(support_speed) && tool == config.opt_int("perimeter_extruder") - 1) {
                                // Skirt uses first material extruder, support material speed.
                                skirt_length += line.dist_XY(self);
                            } else
                                extrusion_points.push_back(scaled(Vec2d{line.new_X(self}), line.new_Y(self)));
                        }
                    }
                    if (self.z() == Approx(0.3) || line.new_Z(self) == Approx(0.3)) {
                        if (line.extruding(self) && self.f() == Approx(support_speed)) {
                        }
                    }
                });
                Slic3r::Polygon convex_hull = Slic3r::Geometry::convex_hull(extrusion_points);
                double hull_perimeter = unscale<double>(convex_hull.split_at_first_point().length());
                REQUIRE(skirt_length > hull_perimeter);
            }
#endif

        }
        WHEN("Large minimum skirt length is used.") {
            config.print.items.opt("min_skirt_length").set(20.0);
            THEN("Gcode generation doesn't crash") {
                REQUIRE(! Slic3r::Test::slice({TestMesh::cube_20x20x20}, config).empty());
            }
        }
    }
}

SCENARIO("Draft shield for levitating objects", "[Skirt][DraftShield]")
{
    using Slic3r::Biz::Algorithms::TriangleMesh::make_cube;

    GIVEN(
        "A levitating 20mm cube (10mm above the bed) with support material and draft shield enabled"
    )
    {
        Domain::TriangleMesh cube = make_cube(20.0, 20.0, 20.0);
        cube.translate(Vec3f(0.0f, 0.0f, 10.0f));

        TestConfig config;
        config.print.items.opt("layer_height").set(0.2);
        config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.2});
        config.print.items.opt("skirts").set(1);
        config.print.items.opt("draft_shield").set(Domain::DraftShield::dsEnabled);
        config.print.items.opt("support_material").set(SupportMode::Everywhere);
        config.print.items.opt("gcode_comments").set(true);

        std::string gcode = Slic3r::Test::slice({cube}, config, false);
        REQUIRE(!gcode.empty());

        THEN("Draft shield (skirt) has no large gaps between layers")
        {
            // Parse G-code and find all Z heights with skirt.
            std::set<double> skirt_layers;
            GCodeReader parser;
            parser.parse_buffer(
                gcode,
                [&skirt_layers](GCodeReader& self, const GCodeReader::GCodeLine& line)
                {
                    if (line.raw().find("; skirt") != std::string::npos) {
                        skirt_layers.insert(self.z());
                    }
                }
            );

            REQUIRE(!skirt_layers.empty());

            // Check that there are no large gaps between consecutive skirt layers.
            // Max allowed gap is 2x layer_height. If there's a gap larger than this,
            // it means draft shield is missing on some support layers.
            const double layer_height    = config.print.items.opt("layer_height").get<double>();
            const double max_allowed_gap = layer_height * 2.;

            double prev_z = 0.;
            for (double z : skirt_layers) {
                if (prev_z > 0) {
                    double gap = z - prev_z;
                    REQUIRE(gap <= max_allowed_gap);
                }

                prev_z = z;
            }
        }
    }
}
