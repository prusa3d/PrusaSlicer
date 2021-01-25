
//#define CATCH_CONFIG_DISABLE

#include <catch2/catch.hpp>
#include <libslic3r/Config.hpp>
#include <libslic3r/Model.hpp>
#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r;
using namespace Slic3r::Test;

SCENARIO("Model construction") {
    GIVEN("A Slic3r Model") {
        Model model{};
        TriangleMesh sample_mesh = make_cube(20,20,20);
        sample_mesh.repair();
        
        DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
        Slic3r::Print print{};
        print.apply(model, config);
        //Slic3r::Test::init_print(print, { sample_mesh }, model, config);

        WHEN("Model object is added") {
            ModelObject* mo = model.add_object();
            mo->name = "cube20";
            THEN("Model object list == 1") {
                REQUIRE(model.objects.size() == 1);
            }

            mo->add_volume(sample_mesh, false);
            THEN("Model volume list == 1") {
                REQUIRE(mo->volumes.size() == 1);
            }
            THEN("Model volume modifier is false") {
                REQUIRE(mo->volumes.front()->is_modifier() == false);
            }
            THEN("Mesh is equivalent to input mesh.") {
                TriangleMesh trimesh = mo->volumes.front()->mesh();
                REQUIRE(sample_mesh.vertices() == trimesh.vertices());
            }
            ModelInstance* inst = mo->add_instance();
            inst->set_rotation(Vec3d(0,0,0));
            inst->set_scaling_factor(Vec3d(1, 1, 1));
            model.arrange_objects(print.config().min_object_distance());
            model.center_instances_around_point(Slic3r::Vec2d(100,100));
            print.auto_assign_extruders(mo);
            //print.add_model_object(mo);
            print.apply(model, config);
            print.validate();
            THEN("Print works?") {
                std::string gcode_filepath{ "" };
                Slic3r::Test::gcode(gcode_filepath, print);
                std::cout << "gcode generation done\n";
                std::string gcode_from_file = read_to_string(gcode_filepath);
                REQUIRE(gcode_from_file.size() > 0);
                clean_file(gcode_filepath, "gcode");
            }

        }
    }

}


SCENARIO("xy compensations"){
    GIVEN(("A Square with a complex hole inside")){
        Slic3r::Polygon square/*new_scale*/{ std::vector<Point>{
            Point{ 100, 100 },
                Point{ 200, 100 },
                Point{ 200, 200 },
                Point{ 100, 200 }} };
        THEN("elephant and xy can compensate each other"){
//TODO
        }
        THEN("hole and xy can compensate each othere"){
        }
    }
}
