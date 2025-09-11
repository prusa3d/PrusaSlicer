#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <LocalesUtils.hpp>
#include <cereal/types/polymorphic.hpp>
#include <cereal/types/string.hpp> 
#include <cereal/types/vector.hpp> 
#include <cereal/archives/binary.hpp>

using namespace Slic3r;

TEST_CASE("Dynamic config serialization - tests ConfigBase", "[Config]"){
    DynamicPrintConfig config;
    INFO("Serialize float");
    config.set_key_value("layer_height", new ConfigOptionFloat(0.3));
    CHECK(config.opt_serialize("layer_height") == "0.3");

    INFO("Serialize int");
    config.set_key_value("perimeters", new ConfigOptionInt(2));
    CHECK(config.opt_serialize("perimeters") == "2");

    INFO("Serialize float or percent");
    config.set_key_value("first_layer_height", new ConfigOptionFloatOrPercent(30, true));
    CHECK(config.opt_serialize("first_layer_height") == "30%");

    INFO("Serialize bool");
    config.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));
    CHECK(config.opt_serialize("use_relative_e_distances") == "1");

    INFO("Serialize enum");
    config.set_key_value("gcode_flavor", new ConfigOptionEnum<GCodeFlavor>(gcfTeacup));
    CHECK(config.opt_serialize("gcode_flavor") == "teacup");

    INFO("Serialize string");
    config.set_key_value("extrusion_axis", new ConfigOptionString("A"));
    CHECK(config.opt_serialize("extrusion_axis") == "A");

    INFO("Serialize string with newline");
    config.set_key_value("notes", new ConfigOptionString("foo\nbar"));
    CHECK(config.opt_serialize("notes") == "foo\\nbar");
    config.set_deserialize_strict("notes", "bar\\nbaz");
    INFO("Deserialize string with newline");
    CHECK(config.opt_string("notes") == "bar\nbaz");

    INFO("Serialize points");
    config.set_key_value("extruder_offset", new ConfigOptionPoints({{10, 20}, {30, 45}}));
    CHECK(config.opt_serialize("extruder_offset") == "10x20,30x45");
    INFO("Deserialize points");
    config.set_deserialize_strict("extruder_offset", "20x10");
    CHECK(config.option<ConfigOptionPoints>("extruder_offset")->values == std::vector{Vec2d{20, 10}});

    INFO("Serialize floats");
    config.set_key_value("nozzle_diameter", new ConfigOptionFloats({0.2, 3}));
    CHECK(config.opt_serialize("nozzle_diameter") == "0.2,3");
    INFO("Deserialize floats");
    config.set_deserialize_strict("nozzle_diameter", "0.1,0.4");
    CHECK_THAT(config.option<ConfigOptionFloats>("nozzle_diameter")->values, Catch::Matchers::Approx(std::vector{0.1, 0.4}));
    INFO("Deserialize floats from one value");
    config.set_deserialize_strict("nozzle_diameter", "3");
    CHECK_THAT(config.option<ConfigOptionFloats>("nozzle_diameter")->values, Catch::Matchers::Approx(std::vector{3.0}));

    INFO("Serialize ints");
    config.set_key_value("temperature", new ConfigOptionInts({180, 210}));
    CHECK(config.opt_serialize("temperature") == "180,210");
    INFO("Deserialize ints");
    config.set_deserialize_strict("temperature", "195,220");
    CHECK(config.option<ConfigOptionInts>("temperature")->values == std::vector{195,220});

    INFO("Serialize bools");
    config.set_key_value("wipe", new ConfigOptionBools({true, false}));
    CHECK(config.opt_serialize("wipe") == "1,0");
    INFO("Deserialize bools");
    config.set_deserialize_strict("wipe", "0,1,1");
    CHECK(config.option<ConfigOptionBools>("wipe")->values == std::vector<unsigned char>{false, true, true});

    INFO("Deserialize bools from empty stirng");
    config.set_deserialize_strict("wipe", "");
    CHECK(config.option<ConfigOptionBools>("wipe")->values == std::vector<unsigned char>{});

    INFO("Deserialize bools from value");
    config.set_deserialize_strict({{"wipe", 1}});
    CHECK(config.option<ConfigOptionBools>("wipe")->values == std::vector<unsigned char>{true});

    INFO("Serialize strings");
    config.set_key_value("post_process", new ConfigOptionStrings({"foo", "bar"}));
    CHECK(config.opt_serialize("post_process") == "foo;bar");
    INFO("Deserialize strings");
    config.set_deserialize_strict("post_process", "bar;baz");
    CHECK(config.option<ConfigOptionStrings>("post_process")->values == std::vector<std::string>{"bar", "baz"});
}

TEST_CASE("Get keys", "[Config]"){
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    CHECK(!config.keys().empty());
}

TEST_CASE("Set not already set option", "[Config]") {
    DynamicPrintConfig config;
    config.set_deserialize_strict("filament_diameter", "3");
}

TEST_CASE("Config apply dynamic to static", "[Config]") {
    DynamicPrintConfig config;
    config.set_deserialize_strict("perimeters", "2");

    // This trick is taken directly from perl.
    StaticPrintConfig* config2 = static_cast<GCodeConfig*>(new FullPrintConfig());
    config2->apply(config, true);

    CHECK(config2->opt_int("perimeters") == 2);
    delete config2;
}

TEST_CASE("Config apply static to dynamic", "[Config]") {
    // This trick is taken directly from perl.
    StaticPrintConfig* config = static_cast<GCodeConfig*>(new FullPrintConfig());

    DynamicPrintConfig config2;
    config2.apply(*config, true);
    delete config;

    CHECK(
        config2.opt_int("perimeters") ==
        DynamicPrintConfig::full_print_config().opt_int("perimeters")
    );

}

TEST_CASE("Config apply dynamic to dynamic", "[Config]") {

    DynamicPrintConfig config;
    config.set_key_value("extruder_offset", new ConfigOptionPoints({{0, 0}, {20, 0}, {0, 20}}));
    DynamicPrintConfig config2;
    config2.apply(config, true);

    CHECK(
        config2.option<ConfigOptionPoints>("extruder_offset")->values ==
        std::vector<Vec2d>{{0, 0}, {20, 0}, {0, 20}}
    );
}

TEST_CASE("Get abs value on percent", "[Config]") {
    StaticPrintConfig* config = static_cast<GCodeConfig*>(new FullPrintConfig());

    config->set_deserialize_strict("solid_infill_speed", "60");
    config->set_deserialize_strict("top_solid_infill_speed", "10%");
    CHECK(config->get_abs_value("top_solid_infill_speed") == 6);
    delete config;
}

TEST_CASE("No interference between DynamicConfig objects", "[Config]") {
    DynamicPrintConfig config;
    config.set_key_value("fill_pattern", new ConfigOptionString("line"));
    DynamicPrintConfig config2;
    config2.set_key_value("fill_pattern", new ConfigOptionString("hilbertcurve"));
    CHECK(config.opt_string("fill_pattern") == "line");
}

TEST_CASE("Normalize fdm extruder", "[Config]") {
    DynamicPrintConfig config;
    config.set("extruder", 2, true);
    config.set("perimeter_extruder", 3, true);
    config.normalize_fdm();
    INFO("Extruder option is removed after normalize().");
    CHECK(!config.has("extruder"));
    INFO("Undefined extruder is populated with default extruder.");
    CHECK(config.opt_int("infill_extruder") == 2);
    INFO("Defined extruder is not overwritten by default extruder.");
    CHECK(config.opt_int("perimeter_extruder") == 3);
}

TEST_CASE("Normalize fdm infill extruder", "[Config]") {
    DynamicPrintConfig config;
    config.set("infill_extruder", 2, true);
    config.normalize_fdm();
    INFO("Undefined solid infill extruder is populated with infill extruder.");
    CHECK(config.opt_int("solid_infill_extruder") == 2);
}

TEST_CASE("Normalize fdm retract layer change", "[Config]") {
    DynamicPrintConfig config;
    config.set("spiral_vase", true, true);
    config.set_key_value("retract_layer_change", new ConfigOptionBools({true, false}));
    config.normalize_fdm();
    CHECK(config.option<ConfigOptionBools>("retract_layer_change")->values == std::vector<unsigned char>{0, 0});
}

TEST_CASE("Can read ini with invalid items", "[Config]") {
    std::string path = std::string(TEST_DATA_DIR) + "/test_config/bad_config_options.ini";

    DynamicPrintConfig config;
    config.load(path, ForwardCompatibilitySubstitutionRule::Disable);
    //Did not crash.
}

struct SerializationTestData {
    std::string name;
    std::vector<std::string> values;
    std::string serialized;
};

TEST_CASE("Config serialization of multiple values", "[Config]"){
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    std::vector<SerializationTestData> test_data{
        {
            "empty",
            {},
            ""
        },
        {
            "single empty",
            {""},
            "\"\""
        },
        {
            "single noempty, simple",
            {"RGB"},
            "RGB"
        },
        {
            "multiple noempty, simple",
            {"ABC", "DEF", "09182745@!#$*(&"},
            "ABC;DEF;09182745@!#$*(&"
        },
        {
            "multiple, simple, some empty",
            {"ABC", "DEF", "", "09182745@!#$*(&", ""},
            "ABC;DEF;;09182745@!#$*(&;"
        },
        {
            "complex",
            {"some \"quoted\" notes", "yet\n some notes", "whatever \n notes", ""},
            "\"some \\\"quoted\\\" notes\";\"yet\\n some notes\";\"whatever \\n notes\";"
        }
    };

    for (const SerializationTestData& data : test_data) {
        config.set_key_value("filament_notes", new ConfigOptionStrings(data.values));
        CHECK(config.opt_serialize("filament_notes") == data.serialized);

        config.set_deserialize_strict("filament_notes", "");
        CHECK(config.option<ConfigOptionStrings>("filament_notes")->values == std::vector<std::string>{});

        config.set_deserialize_strict("filament_notes", data.serialized);
        CHECK(config.option<ConfigOptionStrings>("filament_notes")->values == data.values);
    }
}

SCENARIO("Generic config validation performs as expected.", "[Config]") {
    GIVEN("A config generated from default options") {
        Slic3r::DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        WHEN("perimeter_extrusion_width is set to 250%, a valid value") {
            config.set_deserialize_strict("perimeter_extrusion_width", "250%");
            THEN( "The config is read as valid.") {
                REQUIRE(config.validate().empty());
            }
        }
        WHEN("perimeter_extrusion_width is set to -10, an invalid value") {
            config.set("perimeter_extrusion_width", -10);
            THEN( "Validate returns error") {
                REQUIRE(! config.validate().empty());
            }
        }

        WHEN("perimeters is set to -10, an invalid value") {
            config.set("perimeters", -10);
            THEN( "Validate returns error") {
                REQUIRE(! config.validate().empty());
            }
        }
    }
}

SCENARIO("Config accessor functions perform as expected.", "[Config]") {
    auto test = [](ConfigBase &config) {
        WHEN("A boolean option is set to a boolean value") {
            REQUIRE_NOTHROW(config.set("gcode_comments", true));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing a 0 or 1") {
            CHECK_NOTHROW(config.set_deserialize_strict("gcode_comments", "1"));
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == true);
            }
        }
        WHEN("A boolean option is set to a string value representing something other than 0 or 1") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", "Z"), BadOptionTypeException);
            }
            AND_THEN("Value is unchanged.") {
                REQUIRE(config.opt<ConfigOptionBool>("gcode_comments")->getBool() == false);
            }
        }
        WHEN("A boolean option is set to an int value") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("gcode_comments", 1), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set from serialized string") {
            config.set_deserialize_strict("bed_temperature", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionInts>("bed_temperature")->get_at(0) == 100);
            }
        }
#if 0
        //FIXME better design accessors for vector elements.
        WHEN("An integer-based option is set through the integer interface") {
            config.set("bed_temperature", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionInts>("bed_temperature")->get_at(0) == 100);
            }
        }
#endif
        WHEN("An floating-point option is set through the integer interface") {
            config.set("perimeter_speed", 10);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("perimeter_speed")->getFloat() == 10.0);
            }
        }
        WHEN("A floating-point option is set through the double interface") {
            config.set("perimeter_speed", 5.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionFloat>("perimeter_speed")->getFloat() == 5.5);
            }
        }
        WHEN("An integer-based option is set through the double interface") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("bed_temperature", 5.5), BadOptionTypeException);
            }
        }
        WHEN("A numeric option is set to a non-numeric value.") {
            THEN("A BadOptionTypeException exception is thown.") {
                REQUIRE_THROWS_AS(config.set_deserialize_strict("perimeter_speed", "zzzz"), BadOptionValueException);
            }
            THEN("The value does not change.") {
                REQUIRE(config.opt<ConfigOptionFloat>("perimeter_speed")->getFloat() == 60.0);
            }
        }
        WHEN("A string option is set through the string interface") {
            config.set("end_gcode", "100");
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the integer interface") {
            config.set("end_gcode", 100);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("end_gcode")->value == "100");
            }
        }
        WHEN("A string option is set through the double interface") {
            config.set("end_gcode", 100.5);
            THEN("The underlying value is set correctly.") {
                REQUIRE(config.opt<ConfigOptionString>("end_gcode")->value == float_to_string_decimal_point(100.5));
            }
        }
        WHEN("A float or percent is set as a percent through the string interface.") {
            config.set_deserialize_strict("first_layer_extrusion_width", "100%");
            THEN("Value and percent flag are 100/true") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == true);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the string interface.") {
            config.set_deserialize_strict("first_layer_extrusion_width", "100");
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the int interface.") {
            config.set("first_layer_extrusion_width", 100);
            THEN("Value and percent flag are 100/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100);
            }
        }
        WHEN("A float or percent is set as a float through the double interface.") {
            config.set("first_layer_extrusion_width", 100.5);
            THEN("Value and percent flag are 100.5/false") {
                auto tmp = config.opt<ConfigOptionFloatOrPercent>("first_layer_extrusion_width");
                REQUIRE(tmp->percent == false);
                REQUIRE(tmp->value == 100.5);
            }
        }
        WHEN("An invalid option is requested during set.") {
            THEN("A BadOptionTypeException exception is thrown.") {
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", 1.0), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", "1"), UnknownOptionException);
                REQUIRE_THROWS_AS(config.set("deadbeef_invalid_option", true), UnknownOptionException);
            }
        }

        WHEN("An invalid option is requested during get.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }
        WHEN("An invalid option is requested during opt.") {
            THEN("A UnknownOptionException exception is thrown.") {
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionString>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionFloat>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionInt>("deadbeef_invalid_option", false), UnknownOptionException);
                REQUIRE_THROWS_AS(config.option_throw<ConfigOptionBool>("deadbeef_invalid_option", false), UnknownOptionException);
            }
        }

        WHEN("getX called on an unset option.") {
            THEN("The default is returned.") {
                REQUIRE(config.opt_float("layer_height") == 0.3);
                REQUIRE(config.opt_int("raft_layers") == 0);
                REQUIRE(config.opt_bool("support_material") == false);
            }
        }

        WHEN("getFloat called on an option that has been set.") {
            config.set("layer_height", 0.5);
            THEN("The set value is returned.") {
                REQUIRE(config.opt_float("layer_height") == 0.5);
            }
        }
    };
    GIVEN("DynamicPrintConfig generated from default options") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config();
        test(config);
    }
    GIVEN("FullPrintConfig generated from default options") {
        Slic3r::FullPrintConfig config;
        test(config);
    }
}

SCENARIO("Config ini load/save interface", "[Config]") {
    WHEN("new_from_ini is called") {
		Slic3r::DynamicPrintConfig config;
		std::string path = std::string(TEST_DATA_DIR) + "/test_config/new_from_ini.ini";
		config.load_from_ini(path, ForwardCompatibilitySubstitutionRule::Disable);
        THEN("Config object contains ini file options.") {
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.size() == 1);
			REQUIRE(config.option_throw<ConfigOptionStrings>("filament_colour", false)->values.front() == "#ABCD");
        }
    }
}

SCENARIO("DynamicPrintConfig serialization", "[Config]") {
    WHEN("DynamicPrintConfig is serialized and deserialized") {
        FullPrintConfig full_print_config;
        DynamicPrintConfig cfg;
        cfg.apply(full_print_config, false);

        std::string serialized;
        try {
            std::ostringstream ss;
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(cfg);
            serialized = ss.str();
        } catch (const std::runtime_error & /* e */) {
            // e.what();
        }

        THEN("Config object contains ini file options.") {
            DynamicPrintConfig cfg2;
            try {
                std::stringstream ss(serialized);
                cereal::BinaryInputArchive iarchive(ss);
                iarchive(cfg2);
            } catch (const std::runtime_error & /* e */) {
                // e.what();
            }
            REQUIRE(cfg == cfg2);
        }
    }
}
