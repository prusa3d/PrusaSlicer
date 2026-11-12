#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Domain/Preset/HwConfig.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Model.hpp"

#include <boost/nowide/cstdio.hpp>
#include <boost/filesystem.hpp>

#include "test_data.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

using Biz::Algorithms::ModelObject::ensure_on_bed;
using Biz::Algorithms::ModelObject::add_volume;
using Biz::Slicing::SerializedConfig;
using Domain::Preset::HwPrinterConfig;

SCENARIO("Model construction", "[Model]") {
    GIVEN("A Slic3r Model") {
        Domain::Model model;
        namespace triangle_mesh = Biz::Algorithms::TriangleMesh;
        Domain::TriangleMesh sample_mesh = triangle_mesh::make_cube(20,20,20);
        TestConfig config;
        Slic3r::Print print;

        WHEN("Model object is added") {
            Domain::ModelObject *model_object = model.add_object();
            THEN("Model object list == 1") {
                REQUIRE(model.objects.size() == 1);
            }
            add_volume(model_object, sample_mesh);
            THEN("Model volume list == 1") {
                REQUIRE(model_object->volumes.size() == 1);
            }
            THEN("Model volume is a part") {
                REQUIRE(model_object->volumes.front()->is_model_part());
            }
            THEN("Mesh is equivalent to input mesh.") {
                REQUIRE(! sample_mesh.its.vertices.empty());
				const std::vector<Vec3f>& mesh_vertices = model_object->volumes.front()->mesh().its.vertices;
				Vec3f mesh_offset = model_object->volumes.front()->source.mesh_offset.cast<float>();
				for (size_t i = 0; i < sample_mesh.its.vertices.size(); ++ i) {
					const Vec3f &p1 = sample_mesh.its.vertices[i];
					const Vec3f  p2 = mesh_vertices[i] + mesh_offset;
					REQUIRE((p2 - p1).norm() < EPSILON);
				}
            }

            auto pts = config.get_view().get<std::vector<Vec2d>>("bed_shape");
            Points pts_scaled(pts.size());
            std::transform(pts.cbegin(), pts.cend(), pts_scaled.begin(), [](const Vec2d& pt) { return Slic3r::scaled(pt); });
            Vec2d centroid = unscale(Polygon(pts_scaled).centroid());

            model_object->add_instance();
            model_object->instances.front()->set_offset(Vec3d(centroid.x(), centroid.y(), 0.));

            ensure_on_bed(*model_object);
			THEN("Print works?") {
                Domain::Bed model_bed;
                Domain::BedInstance bed_instance{model_bed};

                for (const Domain::ModelObject* object : model.objects) {
                    for (Domain::ModelInstance* instance : object->instances) {
                        bed_instance.model_instances.push_back(instance);
                    }
                }

                auto preset_metadata =
                    create_dummy_selected_preset_metadata(create_dummy_hw_config());
                auto metadata = Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

                print.update(
                    model,
                    config,
                    bed_instance,
                    preset_metadata,
                    Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config)
                );
			    print.process();
                const Biz::libpgcode::ProcessorResult result{print.process_gcode()};
                CHECK(result.const_gcode()->str().size() > 0);
			}
        }
    }
}
