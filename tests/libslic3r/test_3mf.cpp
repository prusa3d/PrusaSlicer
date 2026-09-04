#include <catch2/catch_test_macros.hpp>

#include <boost/filesystem/operations.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/OnBeds.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Config/3mf_legacy.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"

#include "Slic3r/Math.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"

using namespace Slic3r;
using Slic3r::Domain::Point;
using Slic3r::Domain::Points;
using Slic3r::Domain::Transformation;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::X;

using Biz::Algorithms::Model::flatten_to_mesh;
using Biz::Algorithms::ModelObject::center_around_origin;
using Biz::Algorithms::ModelObject::convex_hull_2d;

SCENARIO("Reading 3mf file", "[3mf]") {
    GIVEN("umlauts in the path of the file") {
        Domain::Model model;
        WHEN("3mf model is read") {
        	std::string path = std::string(TEST_DATA_DIR) + "/test_3mf/Geräte/Büchse.3mf";
        	Domain::ConfigPack config;
            Biz::LegacyPresetMetadata preset_metadata;
            boost::optional<Semver> version;
            Domain::WipeTowersOnBeds wipe_towers;
            Domain::CustomGCodesOnBeds custom_gcodes;
            Biz::VirtualExtrudersConfig virtual_extruders_config;
            bool ret = Slic3rLegacy::load_3mf_legacy(path.c_str(), config, preset_metadata, &model, false, version, wipe_towers, custom_gcodes, virtual_extruders_config);
            THEN("load should succeed") {
                REQUIRE(ret);
            }
        }
    }
}

TEST_CASE("Export+Import geometry to/from 3mf file cycle", "[3mf]") {
    GIVEN("world vertices coordinates before save") {
        // load a model from stl file
        Domain::Model src_model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        Domain::TriangleMesh mesh = Slic3r::Biz::load_stl(src_file).value();

        Slic3r::Biz::Algorithms::Model::add_object(&src_model, "", src_file.c_str(), std::move(mesh));
        src_model.add_default_instances();

        Domain::ModelObject* src_object = src_model.objects.front();

        // apply generic transformation to the 1st volume
        Transformation src_volume_transform;
        src_volume_transform.set_offset({ 10.0, 20.0, 0.0 });
        src_volume_transform.set_rotation({ deg2rad(25.0), deg2rad(35.0), deg2rad(45.0) });
        src_volume_transform.set_scaling_factor({ 1.1, 1.2, 1.3 });
        src_volume_transform.set_mirror({ -1.0, 1.0, -1.0 });
        src_object->volumes.front()->set_transformation(src_volume_transform);

        // apply generic transformation to the 1st instance
        Transformation src_instance_transform;
        src_instance_transform.set_offset({ 5.0, 10.0, 0.0 });
        src_instance_transform.set_rotation({ deg2rad(12.0), deg2rad(13.0), deg2rad(14.0) });
        src_instance_transform.set_scaling_factor({ 0.9, 0.8, 0.7 });
        src_instance_transform.set_mirror({ 1.0, -1.0, -1.0 });
        src_object->instances.front()->set_transformation(src_instance_transform);

        WHEN("model is saved+loaded to/from 3mf file") {
            // save the model to 3mf file
            std::string test_file = std::string(TEST_DATA_DIR) + "/test_3mf/prusa.3mf";
            const Domain::WipeTowersOnBeds src_wipe_towers{
                {0, Domain::ModelWipeTower{Domain::Vec2d{30, 30}, 0.3}}
            };

            const Domain::CustomGCodesOnBeds src_custom_gcodes{
                {0, Domain::CustomGCode::Info{
                    .mode = Domain::CustomGCode::Mode::MultiExtruder,
                    .gcodes = {Domain::CustomGCode::Item{
                        .print_z = 0.5,
                        .type = Domain::CustomGCode::Type::ColorChange,
                        .extruder = 1,
                        .color = "red",
                        .extra = "extra"
                    }}
                }}
            };

            const Domain::Preset::HwPrinterConfig hw_config{Test::create_dummy_hw_config(1)};
            Domain::ConfigPack config;
            Slic3rLegacy::store_3mf_legacy(
                test_file.c_str(),
                &src_model,
                config,
                hw_config,
                false,
                src_wipe_towers,
                src_custom_gcodes
            );

            // load back the model from the 3mf file
            Domain::Model dst_model;
            Domain::ConfigPack dst_config;
            Biz::LegacyPresetMetadata dst_preset_metadata;
            Domain::WipeTowersOnBeds dst_wipe_towers;
            Domain::CustomGCodesOnBeds dst_custom_gcodes;
            {
                boost::optional<Semver> version;
                Biz::VirtualExtrudersConfig dst_virtual_extruders_config;
                Slic3rLegacy::load_3mf_legacy(test_file.c_str(), dst_config, dst_preset_metadata, &dst_model, false, version, dst_wipe_towers, dst_custom_gcodes, dst_virtual_extruders_config);
            }
            boost::filesystem::remove(test_file);

            // compare meshes
            Domain::TriangleMesh src_mesh = flatten_to_mesh(src_model);
            Domain::TriangleMesh dst_mesh = flatten_to_mesh(dst_model);

            bool res = src_mesh.its.vertices.size() == dst_mesh.its.vertices.size();
            if (res) {
                for (size_t i = 0; i < dst_mesh.its.vertices.size(); ++i) {
                    res &= dst_mesh.its.vertices[i].isApprox(src_mesh.its.vertices[i]);
                }
            }
            THEN("world vertices coordinates after load match") {
                REQUIRE(res);
            }
            REQUIRE(dst_wipe_towers.size() == 1);
            REQUIRE(dst_wipe_towers.contains(0));
            CHECK(dst_wipe_towers.at(0) == src_wipe_towers.at(0));

            REQUIRE(dst_custom_gcodes.size() == 1);
            REQUIRE(dst_custom_gcodes.contains(0));
            CHECK(dst_custom_gcodes.at(0) == src_custom_gcodes.at(0));
        }
    }
}

#ifndef __APPLE__
// The ifndef is a hotfix of an intermitted test failure happenning only on macOS.

SCENARIO("2D convex hull of sinking object", "[3mf]") {
    GIVEN("model") {
        // load a model
        Domain::Model model;
        std::string src_file = std::string(TEST_DATA_DIR) + "/test_3mf/Prusa.stl";
        Domain::TriangleMesh mesh = Biz::load_stl(src_file.c_str()).value();

        Slic3r::Biz::Algorithms::Model::add_object(&model, "", src_file.c_str(), std::move(mesh));
        model.add_default_instances();

        WHEN("model is rotated, scaled and set as sinking") {
            Domain::ModelObject* object = model.objects.front();
            center_around_origin(*object, false);

            // set instance's attitude so that it is rotated, scaled and sinking
            Domain::ModelInstance* instance = object->instances.front();
            instance->set_rotation(X, -M_PI / 4.0);
            instance->set_offset(Vec3d::Zero());
            instance->set_scaling_factor({ 2.0, 2.0, 2.0 });

            // calculate 2D convex hull
            Domain::Polygon hull_2d = convex_hull_2d(*object, instance->get_transformation().get_matrix());

            // verify result
            Points expected = {
                { -91501496, -15914144 },
                { 91501496, -15914144 },
                { 91501496, 4243 },
                { 78229680, 4246883 },
                { 56898100, 4246883 },
                { -85501496, 4242641 },
                { -91501496, 4243 }
            };

            THEN("2D convex hull should match with reference") {
                REQUIRE(hull_2d.points.size() == expected.size());
                for (size_t i = 0; i < expected.size(); ++ i) {
                    const Point &p1 = expected[i];
                    const Point &p2 = hull_2d.points[i];
                    // Allow 3um error due to floating point rounding.
                    CHECK(std::abs(p1.x() - p2.x()) <= 3);
                    CHECK(std::abs(p1.y() - p2.y()) <= 3);
                }
            }
        }
    }
}
#endif
