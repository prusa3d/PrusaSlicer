
//#define CATCH_CONFIG_DISABLE

#include <catch2/catch.hpp>
#include "test_data.hpp"
#include <libslic3r/GCodeReader.hpp>

using namespace Slic3r::Test;
using namespace Slic3r;

SCENARIO("skirt test by merill", "") {

    GIVEN("2 objects, don't complete individual object") {
        DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
        // remove noise
        config.set_deserialize("top_solid_layers", "0");
        config.set_deserialize("bottom_solid_layers", "0");
        config.set_deserialize("fill_density", "0");
        config.set_deserialize("perimeters", "1");
        config.set_deserialize("complete_objects", "0");
        config.set_key_value("gcode_comments", new ConfigOptionBool(true));

        WHEN("skirt with 3 layers is requested") {
            config.set_deserialize("skirts", "1");
            config.set_deserialize("skirt_height", "3");
            config.set_deserialize("brim_width", "0");

            Model model{};
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, model, &config);

            std::map<double, bool> layers_with_skirt;
            std::map<double, bool> layers_with_brim;
            std::string gcode_filepath{ "" };
            Slic3r::Test::gcode(gcode_filepath, print);
            auto parser{ Slic3r::GCodeReader() };
            parser.parse_file(gcode_filepath, [&layers_with_skirt, &layers_with_brim, &config](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
            {
                if (line.extruding(self) && line.comment().find("skirt") != std::string::npos) {
                    layers_with_skirt[self.z()] = 1;
                }
                if (line.extruding(self) && line.comment().find("brim") != std::string::npos) {
                    layers_with_brim[self.z()] = 1;
                }
            });
            clean_file(gcode_filepath, "gcode");

            THEN("one skrit generated") {
                REQUIRE(print.skirt().entities.size() == 1);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->skirt().entities.size() == 0);
                }
            }
            THEN("brim is not generated") {
                REQUIRE(print.brim().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->brim().entities.size() == 0);
                }
                REQUIRE(layers_with_brim.size() == 0);
            }
            THEN("skirt_height is honored") {
                REQUIRE(layers_with_skirt.size() == (size_t)config.opt_int("skirt_height"));
            }
        }

        WHEN("brim and skirt with 3 layers is requested") {
            config.set_deserialize("skirts", "1");
            config.set_deserialize("skirt_height", "3");
            config.set_deserialize("brim_width", "4");

            Model model{};
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, model, &config);

            std::map<double, bool> layers_with_skirt;
            std::map<double, bool> layers_with_brim;
            std::string gcode_filepath{ "" };
            Slic3r::Test::gcode(gcode_filepath, print);
            auto parser{ Slic3r::GCodeReader() };
            parser.parse_file(gcode_filepath, [&layers_with_skirt, &layers_with_brim, &config](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
            {
                if (line.extruding(self) && line.comment().find("skirt") != std::string::npos) {
                    layers_with_skirt[self.z()] = 1;
                }
                if (line.extruding(self) && line.comment().find("brim") != std::string::npos) {
                    layers_with_brim[self.z()] = 1;
                }
            });
            clean_file(gcode_filepath, "gcode");

            THEN("one skirt generated") {
                REQUIRE(print.skirt().entities.size() == 1);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->skirt().entities.size() == 0);
                }
            }
            THEN("brim generated") {
                REQUIRE(print.brim().entities.size() > 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->brim().entities.size() == 0);
                }
                REQUIRE(layers_with_brim.size() == 1);
            }
            THEN("skirt_height is honored") {
                REQUIRE(layers_with_skirt.size() == (size_t)config.opt_int("skirt_height"));
            }
        }

        WHEN("brim is requested") {
            config.set_deserialize("skirts", "0");
            config.set_deserialize("skirt_height", "3");
            config.set_deserialize("brim_width", "4");

            Model model{};
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, model, &config);

            std::map<double, bool> layers_with_skirt;
            std::map<double, bool> layers_with_brim;
            std::string gcode_filepath{ "" };
            Slic3r::Test::gcode(gcode_filepath, print);
            auto parser{ Slic3r::GCodeReader() };
            parser.parse_file(gcode_filepath, [&layers_with_skirt, &layers_with_brim, &config](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
            {
                if (line.extruding(self) && line.comment().find("skirt") != std::string::npos) {
                    layers_with_skirt[self.z()] = 1;
                }
                if (line.extruding(self) && line.comment().find("brim") != std::string::npos) {
                    layers_with_brim[self.z()] = 1;
                }
            });
            clean_file(gcode_filepath, "gcode");

            THEN("no skirt generated") {
                REQUIRE(print.skirt().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->skirt().entities.size() == 0);
                }
            }
            THEN("brim generated") {
                REQUIRE(print.brim().entities.size() > 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->brim().entities.size() == 0);
                }
                REQUIRE(layers_with_brim.size() == 1);
            }
            THEN("skirt_height is honored") {
                REQUIRE(layers_with_skirt.size() == 0);
            }
        }
    }

    GIVEN("3 objects, complete individual object") {
        DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
        // remove noise
        config.set_deserialize("top_solid_layers", "0");
        config.set_deserialize("bottom_solid_layers", "0");
        config.set_deserialize("fill_density", "0");
        config.set_deserialize("perimeters", "1");
        config.set_deserialize("complete_objects", "1");
        config.set_key_value("gcode_comments", new ConfigOptionBool(true));

        WHEN("skirt with 3 layers is requested") {
            config.set_deserialize("skirts", "1");
            config.set_deserialize("skirt_height", "3");
            config.set_deserialize("brim_width", "0");

            Model model{};
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20, TestMesh::cube_20x20x20, TestMesh::pyramid }, model, &config);

            std::map<double, bool> layers_with_skirt;
            std::map<double, bool> layers_with_brim;
            std::string gcode_filepath{ "" };
            Slic3r::Test::gcode(gcode_filepath, print);
            auto parser{ Slic3r::GCodeReader() };
            parser.parse_file(gcode_filepath, [&layers_with_skirt, &layers_with_brim, &config](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
            {
                if (line.extruding(self) && line.comment().find("skirt") != std::string::npos) {
                    layers_with_skirt[self.z()] = 1;
                }
                if (line.extruding(self) && line.comment().find("brim") != std::string::npos) {
                    layers_with_brim[self.z()] = 1;
                }
            });
            THEN("one skirt per object") {
                REQUIRE(print.skirt().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->skirt().entities.size() == 1);
                }
            }
            THEN("brim is not generated") {
                REQUIRE(print.brim().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->brim().entities.size() == 0);
                }
                REQUIRE(layers_with_brim.size() == 0);
            }
            THEN("skirt_height is honored") {
                REQUIRE(layers_with_skirt.size() == (size_t)config.opt_int("skirt_height"));
            }
        }

        WHEN("brim and skirt with 3 layers is requested") {
            config.set_deserialize("skirts", "1");
            config.set_deserialize("skirt_height", "3");
            config.set_deserialize("brim_width", "4");

            Model model{};
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, model, &config);

            std::map<double, bool> layers_with_skirt;
            std::map<double, bool> layers_with_brim;
            std::string gcode_filepath{ "" };
            Slic3r::Test::gcode(gcode_filepath, print);
            auto parser{ Slic3r::GCodeReader() };
            parser.parse_file(gcode_filepath, [&layers_with_skirt, &layers_with_brim, &config](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
            {
                if (line.extruding(self) && line.comment().find("skirt") != std::string::npos) {
                    layers_with_skirt[self.z()] = 1;
                }
                if (line.extruding(self) && line.comment().find("brim") != std::string::npos) {
                    layers_with_brim[self.z()] = 1;
                }
            });
            clean_file(gcode_filepath, "gcode");

            THEN("one skirt per object") {
                REQUIRE(print.skirt().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->skirt().entities.size() == 1);
                }
            }
            THEN("brim generated") {
                REQUIRE(print.brim().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->brim().entities.size() > 0);
                }
                REQUIRE(layers_with_brim.size() == 1);
            }
            THEN("skirt_height is honored") {
                REQUIRE(layers_with_skirt.size() == (size_t)config.opt_int("skirt_height"));
            }
        }

        WHEN("brim is requested") {
            config.set_deserialize("skirts", "0");
            config.set_deserialize("skirt_height", "3");
            config.set_deserialize("brim_width", "4");

            Model model{};
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, model, &config);

            std::map<double, bool> layers_with_skirt;
            std::map<double, bool> layers_with_brim;
            std::string gcode_filepath{ "" };
            Slic3r::Test::gcode(gcode_filepath, print);
            auto parser{ Slic3r::GCodeReader() };
            parser.parse_file(gcode_filepath, [&layers_with_skirt, &layers_with_brim, &config](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
            {
                if (line.extruding(self) && line.comment().find("skirt") != std::string::npos) {
                    layers_with_skirt[self.z()] = 1;
                }
                if (line.extruding(self) && line.comment().find("brim") != std::string::npos) {
                    layers_with_brim[self.z()] = 1;
                }
            });
            clean_file(gcode_filepath, "gcode");

            THEN("no skrit") {
                REQUIRE(print.skirt().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->skirt().entities.size() == 0);
                }
            }
            THEN("brim generated") {
                REQUIRE(print.brim().entities.size() == 0);
                for (auto obj : print.objects()) {
                    REQUIRE(obj->brim().entities.size() > 0);
                }
                REQUIRE(layers_with_brim.size() == 1);
            }
            THEN("skirt_height is honored") {
                REQUIRE(layers_with_skirt.size() == 0);
            }
        }
    }
}
SCENARIO("Original Slic3r Skirt/Brim tests", "[!mayfail]") {
    GIVEN("Configuration with a skirt height of 2") {
        DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
        config.set_deserialize("skirts", "1");
        config.set_deserialize("skirt_height", "2");
        config.set_deserialize("perimeters", "1");
        config.set_deserialize("support_material_speed", "99");
        config.set_key_value("gcode_comments", new ConfigOptionBool(true));

        // avoid altering speeds unexpectedly
        config.set_deserialize("cooling", "0");
        config.set_deserialize("first_layer_speed", "100%");

        WHEN("multiple objects are printed") {
            auto gcode {std::stringstream("")};
            Model model{};
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20, TestMesh::cube_20x20x20 }, model, &config, false);
            std::map<double, bool> layers_with_skirt;
            std::string gcode_filepath{ "" };
            Slic3r::Test::gcode(gcode_filepath, print);
            auto parser {Slic3r::GCodeReader()};
            parser.parse_file(gcode_filepath, [&layers_with_skirt, &config] (Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
            {
                if (line.extruding(self) && line.comment().find("skirt") != std::string::npos) {
                    layers_with_skirt[self.z()] = 1;
                }
                //if (self.z() > 0) {
                //    if (line.extruding(self) && (line.new_F(self) == config.opt_float("support_material_speed") * 60.0) ) {
                //        layers_with_skirt[self.z()] = 1;
                //    }
                //}
            });
            clean_file(gcode_filepath, "gcode");

            THEN("skirt_height is honored") {
                REQUIRE(layers_with_skirt.size() == (size_t)config.opt_int("skirt_height"));
            }
        }
    }


    GIVEN("A default configuration") {
        DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
        config.set_deserialize("support_material_speed", "99");

        // avoid altering speeds unexpectedly
        config.set_deserialize("cooling", "0");
        config.set_deserialize("first_layer_speed", "100%");
        // remove noise from top/solid layers
        config.set_deserialize("top_solid_layers", "0");
        config.set_deserialize("bottom_solid_layers", "0");

        WHEN("Brim width is set to 5") {
            config.set_deserialize("perimeters", "0");
            config.set_deserialize("skirts", "0");
            config.set_deserialize("brim_width", "5");
            THEN("Brim is generated") {
                Model model{};
                Print print{};
                Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20 }, model, &config, false);
                print.process();
                REQUIRE(print.brim().entities.size()>0);
            }
        }

        WHEN("Skirt area is smaller than the brim") {
            config.set_deserialize("skirts", "1");
            config.set_deserialize("brim_width", "10");
            THEN("GCode generates successfully.") {
                Model model{};
                Print print{};
                Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20 }, model, &config, false);
                std::string gcode_filepath{ "" };
                Slic3r::Test::gcode(gcode_filepath, print);
                std::string gcode_from_file = read_to_string(gcode_filepath);
                REQUIRE(gcode_from_file.size() > 0);
                clean_file(gcode_filepath, "gcode");
            }
        }

        WHEN("Skirt height is 0 and skirts > 0") {
            config.set_deserialize("skirts", "2");
            config.set_deserialize("skirt_height", "0");

            THEN("GCode generates successfully.") {
                Model model{};
                Print print{};
                Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20 }, model, &config, false);
                std::string gcode_filepath{ "" };
                Slic3r::Test::gcode(gcode_filepath, print);
                std::string gcode_from_file = read_to_string(gcode_filepath);
                REQUIRE(gcode_from_file.size() > 0);
                clean_file(gcode_filepath, "gcode");
            }
        }

        WHEN("Perimeter extruder = 2 and support extruders = 3") {
            THEN("Brim is printed with the extruder used for the perimeters of first object") {
                REQUIRE(true); //TODO
            }
        }
        WHEN("Perimeter extruder = 2, support extruders = 3, raft is enabled") {
            THEN("brim is printed with same extruder as skirt") {
                REQUIRE(true); //TODO
            }
        }

        //TODO review this test that fail because "there are no layer" and it test nothing anyway
        //WHEN("Object is plated with overhang support and a brim") {
        //    config.set_deserialize("layer_height", "0.4");
        //    config.set_deserialize("first_layer_height", "0.4");
        //    config.set_deserialize("skirts", "1");
        //    config.set_deserialize("skirt_distance", "0");
        //    config.set_deserialize("support_material_speed", "99");
        //    config.set_deserialize("perimeter_extruder", "1");
        //    config.set_deserialize("support_material_extruder", "2");
        //    config.set_deserialize("cooling", "0");                     // to prevent speeds to be altered
        //    config.set_deserialize("first_layer_speed", "100%");      // to prevent speeds to be altered

        //    Slic3r::Model model;
        //    Print print{};
        //    Slic3r::Test::init_print(print, { TestMesh::overhang }, model, &config, false);
        //    print.process();
        //    
        //    config.set_deserialize("support_material", "true");      // to prevent speeds to be altered

        //    THEN("skirt length is large enough to contain object with support") {
        //        REQUIRE(true); //TODO
        //    }
        //}
        WHEN("Large minimum skirt length is used.") {
            config.set_deserialize("min_skirt_length", "20");
            auto gcode {std::stringstream("")};
            Slic3r::Model model;
            Print print{};
            Slic3r::Test::init_print(print, { TestMesh::cube_20x20x20 }, model, &config, false);
            THEN("Gcode generation doesn't crash") {
                std::string gcode_filepath{ "" };
                Slic3r::Test::gcode(gcode_filepath, print);
                std::string gcode_from_file = read_to_string(gcode_filepath);
                REQUIRE(gcode_from_file.size() > 0);
                clean_file(gcode_filepath, "gcode");
            }
        }
    }
}
