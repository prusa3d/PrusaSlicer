
//#define CATCH_CONFIG_DISABLE

#include <catch2/catch.hpp>
#include <string>
#include "test_data.hpp"
#include <libslic3r/libslic3r.h>
#include <libslic3r/ExtrusionEntityCollection.hpp>
#include <libslic3r/ExtrusionEntity.hpp>
#include <libslic3r/Point.hpp>
#include <libslic3r/GCodeReader.hpp>
#include <cstdlib>

using namespace Slic3r;

Slic3r::Point random_point(float LO=-50, float HI=50) {
    float x = LO + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(HI-LO)));
    float y = LO + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(HI-LO)));
    return Slic3r::Point(x, y);
}

// build a sample extrusion entity collection with random start and end points.
Slic3r::ExtrusionPath random_path(size_t length = 20, float LO=-50, float HI=50) {
    Slic3r::ExtrusionPath t { Slic3r::ExtrusionRole::erPerimeter, 1.0, 1.0, 1.0};
    for (size_t j = 0; j < length; j++) {
        t.polyline.append(random_point(LO, HI));
    }
    return t;
}

Slic3r::ExtrusionPaths random_paths(size_t count=10, size_t length=20, float LO=-50, float HI=50) {
    Slic3r::ExtrusionPaths p;
    for (size_t i = 0; i < count; i++)
        p.push_back(random_path(length, LO, HI));
    return p;
}

using namespace Slic3r;

SCENARIO("ExtrusionEntityCollection: Polygon flattening") {
    srand(0xDEADBEEF); // consistent seed for test reproducibility.

    // Generate one specific random path set and save it for later comparison
    Slic3r::ExtrusionPaths nosort_path_set {random_paths()};

    Slic3r::ExtrusionEntityCollection sub_nosort;
    sub_nosort.append(nosort_path_set);
    sub_nosort.no_sort = true;

    Slic3r::ExtrusionEntityCollection sub_sort;
    sub_sort.no_sort = false;
    sub_sort.append(random_paths());


    GIVEN("A Extrusion Entity Collection with a child that has one child that is marked as no-sort") {
        Slic3r::ExtrusionEntityCollection sample;

        sample.append(sub_sort);
        sample.append(sub_nosort);
        sample.append(sub_sort);
        WHEN("The EEC is flattened with default options (preserve_order=false)") {
            Slic3r::ExtrusionEntityCollection output = Slic3r::FlatenEntities(false).flatten(sample);
            THEN("The output EEC contains no Extrusion Entity Collections") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 0);
            }
        }
        WHEN("The EEC is flattened with preservation (preserve_order=true)") {
            Slic3r::ExtrusionEntityCollection output = Slic3r::FlatenEntities(true).flatten(sample);
            THEN("The output EECs contains one EEC.") {
                CHECK(std::count_if(output.entities.cbegin(), output.entities.cend(), [=](const ExtrusionEntity* e) {return e->is_collection();}) == 1);
            }
            AND_THEN("The ordered EEC contains the same order of elements than the original") {
                // find the entity in the collection
                for (auto e : output.entities) {
                    if (!e->is_collection()) continue;
                    Slic3r::ExtrusionEntityCollection* temp = dynamic_cast<ExtrusionEntityCollection*>(e);
                    // check each Extrusion path against nosort_path_set to see if the first and last match the same
                    CHECK(nosort_path_set.size() == temp->entities.size());
                    for (size_t i = 0; i < nosort_path_set.size(); i++) {
                        CHECK(temp->entities[i]->first_point() == nosort_path_set[i].first_point());
                        CHECK(temp->entities[i]->last_point() == nosort_path_set[i].last_point());
                    }
                }
            }
        }
    }
}

SCENARIO("ExtrusionEntityCollection: no sort") {
    DynamicPrintConfig &config = Slic3r::DynamicPrintConfig::full_print_config();
    config.set_key_value("gcode_comments", new ConfigOptionBool(true));
    config.set_deserialize("skirts", "0");
    Model model{};
    Print print{};
    Slic3r::Test::init_print(print, { Slic3r::Test::TestMesh::cube_20x20x20}, model, &config);

    std::map<double, bool> layers_with_skirt;
    std::map<double, bool> layers_with_brim;
    std::string gcode_filepath{ "" };

    print.process();
    //replace extrusion from sliceing by manual ones
    print.objects()[0]->clear_layers();
    Layer* customL_layer = print.objects()[0]->add_layer(0, 0.2, 0.2, 0.1);
    LayerRegion* custom_region = customL_layer->add_region(print.regions()[0]);

    ExtrusionPath path_peri(ExtrusionRole::erPerimeter);
    path_peri.polyline.append(Point{ 0,0 });
    path_peri.polyline.append(Point{ scale_(1),scale_(0) });
    ExtrusionPath path_fill1(ExtrusionRole::erInternalInfill);
    path_fill1.polyline.append(Point{ scale_(1),scale_(0) });
    path_fill1.polyline.append(Point{ scale_(2),scale_(0) });
    ExtrusionPath path_fill2(ExtrusionRole::erInternalInfill);
    path_fill2.polyline.append(Point{ scale_(2),scale_(0) });
    path_fill2.polyline.append(Point{ scale_(3),scale_(0) });
    ExtrusionEntityCollection coll_fill;
    coll_fill.append(path_fill2);
    coll_fill.append(path_fill1);
    ExtrusionEntityCollection coll_peri;
    coll_peri.append(path_peri);


    WHEN("sort") {
        custom_region->fills.append(coll_fill);
        custom_region->perimeters.append(coll_peri);
        coll_fill.no_sort = false;
        Slic3r::Test::gcode(gcode_filepath, print);
        auto parser{ Slic3r::GCodeReader() };
        std::vector<float> extrude_x;
        parser.parse_file(gcode_filepath, [&extrude_x](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
        {
            if (line.comment() == " infill" || line.comment() == " perimeter" || line.comment() == " move to first infill point") {
                extrude_x.push_back(line.x());
            }
        });
        Slic3r::Test::clean_file(gcode_filepath, "gcode");
        REQUIRE(extrude_x.size()==3);
        REQUIRE(extrude_x[0] == 91);
        REQUIRE(extrude_x[1] == 92);
        REQUIRE(extrude_x[2] == 93);
    }


    WHEN("no sort") {
        coll_fill.no_sort = true;
        custom_region->fills.append(coll_fill);
        custom_region->perimeters.append(coll_peri);
        Slic3r::Test::gcode(gcode_filepath, print);
        auto parser{ Slic3r::GCodeReader() };
        std::vector<float> extrude_x;
        parser.parse_file(gcode_filepath, [&extrude_x](Slic3r::GCodeReader& self, const Slic3r::GCodeReader::GCodeLine& line)
        {
            if (line.comment() == " infill" || line.comment() == " perimeter" || line.comment() == " move to first infill point") {
                extrude_x.push_back(line.x());
            }
        });
        Slic3r::Test::clean_file(gcode_filepath, "gcode");
        REQUIRE(extrude_x.size() == 5);
        REQUIRE(extrude_x[0] == 91);
        REQUIRE(extrude_x[1] == 92);
        REQUIRE(extrude_x[2] == 93);
        REQUIRE(extrude_x[3] == 91);
        REQUIRE(extrude_x[4] == 92);
    }
}
