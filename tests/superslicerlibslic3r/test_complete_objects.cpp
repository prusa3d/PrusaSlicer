
#define CATCH_CONFIG_DISABLE
#include <catch2/catch.hpp>
//#include <catch_main.hpp>

#include <string>
#include "test_data.hpp"
#include <libslic3r/libslic3r.h>
#include <libslic3r/ModelArrange.hpp>
#include <libslic3r/SVG.hpp>

using namespace Slic3r::Test;
using namespace Slic3r;
using namespace std::literals;


std::unique_ptr<Print> init_print_with_dist(DynamicPrintConfig &config, float distance) {
    TestMesh m = TestMesh::cube_20x20x20;
    Model model{};

    std::unique_ptr<Print> print(new Print{});
    ModelObject* object{ model.add_object() };
    object->name += std::string(mesh_names.at(m)) + ".stl"s;
    object->add_volume(mesh(m));

    ModelInstance* inst1{ object->add_instance() };
    inst1->set_offset(Vec3d(-distance/2, 0, 0));
    inst1->set_rotation(Vec3d(0, 0, 0));
    inst1->set_scaling_factor(Vec3d(1, 1, 1));

    ModelInstance* inst2{ object->add_instance() };
    inst2->set_offset(Vec3d(distance/2, 0, 0));
    inst2->set_rotation(Vec3d(0, 0, 0));
    inst2->set_scaling_factor(Vec3d(1, 1, 1));
    for (auto* mo : model.objects) {
        print->auto_assign_extruders(mo);
    }

    if (distance <= 0) {
        print->apply(model, config);
        arrange_objects(model, InfiniteBed{}, ArrangeParams{ scaled(/*min_object_distance(config)) });/*/ PrintConfig::min_object_distance(&print->config(), 999999)) });
        model.center_instances_around_point(Slic3r::Vec2d(100, 100));
    }

    std::cout << "inst1 pos = " << inst1->get_offset().x() << ":" << inst1->get_offset().y() << "\n";
    std::cout << "inst2 pos = " << inst2->get_offset().x() << ":" << inst2->get_offset().y() << "\n";

    print->apply(model, config);
    return print;
}

SCENARIO("Complete objects separatly") {
    GIVEN("20mm cubes and extruder_clearance_radius to 10") {
        DynamicPrintConfig& config = Slic3r::DynamicPrintConfig::full_print_config();
        config.set_key_value("fill_density", new ConfigOptionPercent(0));
        config.set_deserialize("nozzle_diameter", "0.4");
        config.set_deserialize("layer_height", "0.3");
        config.set_deserialize("extruder_clearance_height", "50");
        config.set_deserialize("extruder_clearance_radius", "10");
        config.set_deserialize("skirts", "0");
        config.set_deserialize("skirt_height", "0");
        config.set_deserialize("brim_width", "0");

        std::pair<PrintBase::PrintValidationError, std::string>  result;

        WHEN("2 mm appart") {

            //model.arrange_objects(print.config().min_object_distance());
            //model.center_instances_around_point(Slic3r::Vec2d(100, 100));
            THEN("no complete objects") {
                result = init_print_with_dist(config, 22)->validate();
                REQUIRE(result.second == "");
            }

            //now with complete_objects
            THEN("complete objects") {
                config.set_key_value("complete_objects", new ConfigOptionBool(true));
                result = init_print_with_dist(config, 22)->validate();
                REQUIRE(result.first == PrintBase::PrintValidationError::pveWrongPosition);
            }
        }
        WHEN("at the limit (~30 mm)") {
            config.set_key_value("complete_objects", new ConfigOptionBool(true));
            THEN("(too near)") {
                result = init_print_with_dist(config, 29.9)->validate();
                REQUIRE(result.first == PrintBase::PrintValidationError::pveWrongPosition);
            }
            THEN("(ok far)") {
                result = init_print_with_dist(config, 30.1)->validate();
                REQUIRE(result.second == "");
                REQUIRE(result.first == PrintBase::PrintValidationError::pveNone);
            }
        }
        WHEN("with a 10 mm brim, so the dist should be 40mm ") {
            config.set_key_value("complete_objects", new ConfigOptionBool(true));
            config.set_deserialize("brim_width", "10");
            THEN("(too near)") {
                result = init_print_with_dist(config, 39.9)->validate();
                REQUIRE(result.first == PrintBase::PrintValidationError::pveWrongPosition);
            }
            THEN("(ok far)") {
                result = init_print_with_dist(config, 40.1)->validate();
                REQUIRE(result.second == "");
                REQUIRE(result.first == PrintBase::PrintValidationError::pveNone);
            }
        }
        WHEN("with a 10 mm dist short skirt, so the dist should be 40mm +extrusionwidth") {
            config.set_key_value("complete_objects", new ConfigOptionBool(true));
            config.set_deserialize("skirts", "1");
            config.set_deserialize("skirt_height", "1");
            config.set_deserialize("skirt_distance", "10");
            config.set_deserialize("complete_objects_one_skirt", "0");
            
            THEN("(too near)") {
                result = init_print_with_dist(config, 40)->validate();
                REQUIRE(result.first == PrintBase::PrintValidationError::pveWrongPosition);
            }
            THEN("(ok far)") {
                result = init_print_with_dist(config, 40.8)->validate();
                REQUIRE(result.second == "");
                REQUIRE(result.first == PrintBase::PrintValidationError::pveNone);
            }
        }
    }
}
SCENARIO("Arrange is good enough") {
    GIVEN("20mm cubes and extruder_clearance_radius to 10") {
        DynamicPrintConfig& config = Slic3r::DynamicPrintConfig::full_print_config();
        config.set_key_value("fill_density", new ConfigOptionPercent(0));
        config.set_deserialize("nozzle_diameter", "0.4");
        config.set_deserialize("layer_height", "0.3");
        config.set_deserialize("extruder_clearance_height", "50");
        config.set_deserialize("extruder_clearance_radius", "10");
        config.set_deserialize("skirts", "0");
        config.set_deserialize("skirt_height", "0");
        config.set_deserialize("brim_width", "0");

        std::pair<PrintBase::PrintValidationError, std::string>  result;

        WHEN("no complete objects") {
            result = init_print_with_dist(config,-1)->validate();
            REQUIRE(result.second == "");
        }
        WHEN("complete objects") {
            config.set_key_value("complete_objects", new ConfigOptionBool(true));
            result = init_print_with_dist(config, -1)->validate();
            REQUIRE(result.second == "");
        }
        WHEN("complete objects whith brim") {
            config.set_key_value("complete_objects", new ConfigOptionBool(true));
            config.set_deserialize("brim_width", "10");
            result = init_print_with_dist(config, -1)->validate();
            REQUIRE(result.second == "");
        }
        WHEN("complete objects whith skirt") {
            config.set_key_value("complete_objects", new ConfigOptionBool(true));
            config.set_deserialize("skirts", "1");
            config.set_deserialize("skirt_height", "1");
            config.set_deserialize("skirt_distance", "10");
            config.set_deserialize("complete_objects_one_skirt", "0");
            result = init_print_with_dist(config, -1)->validate();
            REQUIRE(result.second == "");
        }
    }
}
