#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/ConfigUtils.hpp"
#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"

#include "test_data.hpp"

#include <algorithm>
#include <boost/regex.hpp>

using namespace Slic3r;
using namespace Slic3r::Test;
using namespace Catch;
using Biz::GCodeReader::GCodeReader;
using Domain::FloatOrPercentage;
using Domain::Percentage;
using Domain::SupportMode;

boost::regex perimeters_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; perimeter");
boost::regex infill_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; infill");
boost::regex skirt_regex("G1 X[-0-9.]* Y[-0-9.]* E[-0-9.]* ; skirt");

TEST_CASE( "PrintGCode basic functionality", "[PrintGCode]") {
    GIVEN("A default configuration and a print test object") {
        WHEN("the output is executed with no support material") {
            Slic3r::Print print;
            Slic3r::Domain::Model model;

            TestConfig config;
            config.print.items.opt("layer_height").set(0.2);
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.2});
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{0});
            config.print.items.opt("support_material").set(SupportMode::None);
            config.print.items.opt("gcode_comments").set(true);
            config.printer.items.opt("start_gcode").set("");

            Slic3r::Test::init_print({TestMesh::cube_20x20x20}, print, model, config);
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains slic3r version") {
                REQUIRE(gcode.find(SLIC3R_VERSION) != std::string::npos);
            }
            //THEN("Exported text contains git commit id") {
            //    REQUIRE(gcode.find("; Git Commit") != std::string::npos);
            //    REQUIRE(gcode.find(SLIC3R_BUILD_ID) != std::string::npos);
            //}
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") == std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") == std::string::npos);
            }
            THEN("Exported text does not contain cooling markers (they were consumed)") {
                REQUIRE(gcode.find(";_EXTRUDE_SET_SPEED") == std::string::npos);
            }

            THEN("GCode preamble is emitted.") {
                REQUIRE(gcode.find("G21 ; set units to millimeters") != std::string::npos);
            }

            THEN("Config options emitted for print config, default region config, default object config") {
                REQUIRE(gcode.find("; first_layer_temperature") != std::string::npos);
                REQUIRE(gcode.find("; layer_height") != std::string::npos);
                REQUIRE(gcode.find("; fill_density") != std::string::npos);
            }
            THEN("Infill is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, infill_regex));
            }
            THEN("Perimeters are emitted.") {
				boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, perimeters_regex));
            }
            THEN("Skirt is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, skirt_regex));
            }
            THEN("final Z height is 20mm") {
                double final_z = 0.0;
                GCodeReader reader;

                const std::string axis{get_extrusion_axis(print.config())};
                reader.set_extrusion_axis(axis.empty() ? 0 : axis[0]);
                reader.set_use_relative_e_distances(print.config().get<bool>("use_relative_e_distances"));
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max<double>(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.));
            }
        }
        WHEN("output is executed with complete objects and two differently-sized meshes") {
            Slic3r::Print print;
            Slic3r::Domain::Model model;

            TestConfig config;
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{0});
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.3});
            config.print.items.opt("layer_height").set(0.2);
            config.print.items.opt("support_material").set(SupportMode::None);
            config.print.items.opt("raft_layers").set(0);
            config.print.items.opt("complete_objects").set(true);
            config.print.items.opt("gcode_comments").set(true);
            config.printer.items.opt("between_objects_gcode").set("; between-object-gcode");

            Slic3r::Test::init_print({TestMesh::cube_20x20x20,TestMesh::cube_20x20x20}, print, model, config);
            std::string gcode = Slic3r::Test::gcode(print);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Infill is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, infill_regex));
            }
            THEN("Perimeters are emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, perimeters_regex));
            }
            THEN("Skirt is emitted.") {
                boost::smatch has_match;
                REQUIRE(boost::regex_search(gcode, has_match, skirt_regex));
            }
            THEN("Between-object-gcode is emitted.") {
                REQUIRE(gcode.find("; between-object-gcode") != std::string::npos);
            }
            THEN("final Z height is 20.1mm") {
                double final_z = 0.0;
                GCodeReader reader;
                const std::string axis{get_extrusion_axis(print.config())};
                reader.set_extrusion_axis(axis.empty() ? 0 : axis[0]);
                reader.set_use_relative_e_distances(print.config().get<bool>("use_relative_e_distances"));
                reader.parse_buffer(gcode, [&final_z] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                });
                REQUIRE(final_z == Approx(20.1));
            }
            THEN("Z height resets on object change") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                const std::string axis{get_extrusion_axis(print.config())};
                reader.set_extrusion_axis(axis.empty() ? 0 : axis[0]);
                reader.set_use_relative_e_distances(print.config().get<bool>("use_relative_e_distances"));
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { // saw higher Z before this, now it's lower
                        reset = true;
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
            THEN("Shorter object is printed before taller object.") {
                double final_z = 0.0;
                bool reset = false;
                GCodeReader reader;
                const std::string axis{get_extrusion_axis(print.config())};
                reader.set_extrusion_axis(axis.empty() ? 0 : axis[0]);
                reader.set_use_relative_e_distances(print.config().get<bool>("use_relative_e_distances"));
                reader.parse_buffer(gcode, [&final_z, &reset] (GCodeReader& self, const GCodeReader::GCodeLine& line) {
                    if (final_z > 0 && std::abs(self.z() - 0.3) < 0.01 ) { 
                        reset = (final_z > 20.0);
                    } else {
                        final_z = std::max(final_z, static_cast<double>(self.z())); // record the highest Z point we reach
                    }
                });
                REQUIRE(reset == true);
            }
        }
        WHEN("the output is executed with support material") {
            TestConfig config;
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{0});
            config.print.items.opt("support_material").set(SupportMode::Everywhere);
            config.print.items.opt("raft_layers").set(3);
            config.print.items.opt("gcode_comments").set(true);

            std::string gcode = ::Test::slice({TestMesh::cube_20x20x20}, config);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") == std::string::npos);
            }
            THEN("Raft is emitted.") {
                REQUIRE(gcode.find("; raft") != std::string::npos);
            }
        }
        WHEN("the output is executed with a separate first layer extrusion width") {
            TestConfig config;
            config.print.items.opt("first_layer_extrusion_width").set(FloatOrPercentage{0.5});
            config.print.items.opt("support_material").set(SupportMode::None);
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, config);
            THEN("Some text output is generated.") {
                REQUIRE(gcode.size() > 0);
            }
            THEN("Exported text contains extrusion statistics.") {
                REQUIRE(gcode.find("; external perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; perimeters extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; solid infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; top infill extrusion width") != std::string::npos);
                REQUIRE(gcode.find("; support material extrusion width") == std::string::npos);
                REQUIRE(gcode.find("; first layer extrusion width") != std::string::npos);
            }
        }
        WHEN("Cooling is enabled and the fan is disabled.") {

            TestConfig config;
            config.filament.at(0).items.opt("cooling").set(true);
            config.filament.at(0).items.opt("disable_fan_first_layers").set(5);
			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, config);
            THEN("GCode to disable fan is emitted."){
                REQUIRE(gcode.find("M107") != std::string::npos);
            }
        }
        WHEN("end_gcode exists with layer_num and layer_z") {

            TestConfig config;
            config.printer.items.opt("end_gcode").set("; Layer_num [layer_num]\n; Layer_z [layer_z]");
            config.print.items.opt("layer_height").set(0.1);
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.1});

			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, config);
            THEN("layer_num and layer_z are processed in the end gcode") {
                REQUIRE(gcode.find("; Layer_num 199") != std::string::npos);
                REQUIRE(gcode.find("; Layer_z 20") != std::string::npos);
            }
        }
        WHEN("current_extruder exists in start_gcode") {
            {
                TestConfig config;
                config.printer.items.opt("start_gcode").set("; Extruder [current_extruder]");
				std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20 }, config);
                THEN("current_extruder is processed in the start gcode and set for first extruder") {
                    REQUIRE(gcode.find("; Extruder 0") != std::string::npos);
                }
            }
			{
                TestConfig config{4};
                config.printer.items.opt("start_gcode").set("; Extruder [current_extruder]");
                config.print.items.opt("infill_extruder").set(2);
                config.print.items.opt("solid_infill_extruder").set(2);
                config.print.items.opt("perimeter_extruder").set(2);
                config.print.items.opt("support_material_extruder").set(2);
                config.print.items.opt("support_material_interface_extruder").set(2);

                std::string gcode = Slic3r::Test::slice({TestMesh::cube_20x20x20}, config);
                THEN("current_extruder is processed in the start gcode and set for second extruder") {
                    REQUIRE(gcode.find("; Extruder 1") != std::string::npos);
                }
            }
        }

        WHEN("layer_num represents the layer's index from z=0") {

            TestConfig config;
            config.print.items.opt("complete_objects").set(true );
            config.print.items.opt("gcode_comments").set(true );
            config.printer.items.opt("layer_gcode").set(";Layer:[layer_num] ([layer_z] mm)");
            config.print.items.opt("layer_height").set(0.1);
            config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.1});

			std::string gcode = ::Test::slice({ TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, config);
			// End of the 1st object.
            std::string token = ";Layer:199 ";
			size_t pos = gcode.find(token);
			THEN("First and second object last layer is emitted") {
				// First object
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				double z = 0;
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
				// Second object
				pos = gcode.find(";Layer:399 ", pos);
				REQUIRE(pos != std::string::npos);
				pos += token.size();
				REQUIRE(pos < gcode.size());
				REQUIRE((sscanf(gcode.data() + pos, "(%lf mm)", &z) == 1));
				REQUIRE(z == Approx(20.));
			}
        }
    }
}
