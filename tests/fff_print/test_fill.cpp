#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <sstream>

#include "libslic3r/libslic3r.h"

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SVG.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace std::literals;

bool test_if_solid_surface_filled(const ExPolygon& expolygon, double flow_spacing, double angle = 0, double density = 1.0);

#if 0
TEST_CASE("Fill: adjusted solid distance") {
    int surface_width = 250;
    int distance = Slic3r::Flow::solid_spacing(surface_width, 47);
    REQUIRE(distance == Approx(50));
    REQUIRE(surface_width % distance == 0);
}
#endif

TEST_CASE("Fill: Pattern Path Length", "[Fill]") {
    std::unique_ptr<Slic3r::Fill> filler(Slic3r::Fill::new_from_type("rectilinear"));
    filler->angle = float(-(PI)/2.0);
	FillParams fill_params;
	filler->spacing = 5;
	fill_params.dont_adjust = true;
	//fill_params.endpoints_overlap = false;
	fill_params.density = float(filler->spacing / 50.0);

    auto test = [&filler, &fill_params] (const ExPolygon& poly) -> Slic3r::Polylines {
        Slic3r::Surface surface(stTop, poly);
        return filler->fill_surface(&surface, fill_params);
    };

    SECTION("Square") {
        Slic3r::Points test_set;
        test_set.reserve(4);
        std::vector<Vec2d> points { {0,0}, {100,0}, {100,100}, {0,100} };
        for (size_t i = 0; i < 4; ++i) {
            std::transform(points.cbegin()+i, points.cend(),   std::back_inserter(test_set), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } ); 
            std::transform(points.cbegin(), points.cbegin()+i, std::back_inserter(test_set), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );
            Slic3r::Polylines paths = test(Slic3r::ExPolygon(test_set));
            REQUIRE(paths.size() == 1); // one continuous path

            // TODO: determine what the "Expected length" should be for rectilinear fill of a 100x100 polygon. 
            // This check only checks that it's above scale(3*100 + 2*50) + scaled_epsilon.
            // ok abs($paths->[0]->length - scale(3*100 + 2*50)) - scaled_epsilon, 'path has expected length';
            REQUIRE(std::abs(paths[0].length() - static_cast<double>(scale_(3*100 + 2*50))) - SCALED_EPSILON > 0); // path has expected length

            test_set.clear();
        }
    }
    SECTION("Diamond with endpoints on grid") {
        std::vector<Vec2d> points {Vec2d(0,0), Vec2d(100,0), Vec2d(150,50), Vec2d(100,100), Vec2d(0,100), Vec2d(-50,50)};
        Slic3r::Points test_set;
        test_set.reserve(6);
        std::transform(points.cbegin(), points.cend(),   std::back_inserter(test_set), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );
        Slic3r::Polylines paths = test(Slic3r::ExPolygon(test_set));
        REQUIRE(paths.size() == 1); // one continuous path
    }

    SECTION("Square with hole") {
        std::vector<Vec2d> square {Vec2d(0,0), Vec2d(100,0), Vec2d(100,100), Vec2d(0,100)};
        std::vector<Vec2d> hole {Vec2d(25,25), Vec2d(75,25), Vec2d(75,75), Vec2d(25,75) };
        std::reverse(hole.begin(), hole.end());

        Slic3r::Points test_hole;
        Slic3r::Points test_square;

        std::transform(square.cbegin(), square.cend(), std::back_inserter(test_square), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );
        std::transform(hole.cbegin(), hole.cend(), std::back_inserter(test_hole), [] (const Vec2d& a) -> Point { return Point::new_scale(a.x(), a.y()); } );

        for (double angle : {-(PI/2.0), -(PI/4.0), -(PI), PI/2.0, PI}) {
            for (double spacing : {25.0, 5.0, 7.5, 8.5}) {
				fill_params.density = float(filler->spacing / spacing);
                filler->angle = float(angle);
                ExPolygon e(test_square, test_hole);
                Slic3r::Polylines paths = test(e);
#if 0
				{
					BoundingBox bbox = get_extents(e);
					SVG svg("c:\\data\\temp\\square_with_holes.svg", bbox);
					svg.draw(e);
					svg.draw(paths);
					svg.Close();
				}
#endif
                REQUIRE((paths.size() >= 1 && paths.size() <= 3));
                // paths don't cross hole
                REQUIRE(diff_pl(paths, offset(e, float(SCALED_EPSILON*10))).size() == 0);
            }
        }
    }
    SECTION("Regression: Missing infill segments in some rare circumstances") {
        filler->angle = float(PI/4.0);
		fill_params.dont_adjust = false;
        filler->spacing = 0.654498;
        //filler->endpoints_overlap = unscale(359974);
		fill_params.density = 1;
        filler->layer_id = 66;
        filler->z = 20.15;

        Slic3r::Points points {Point(25771516,14142125),Point(14142138,25771515),Point(2512749,14142131),Point(14142125,2512749)};
        Slic3r::Polylines paths = test(Slic3r::ExPolygon(points));
        REQUIRE(paths.size() == 1); // one continuous path

        // TODO: determine what the "Expected length" should be for rectilinear fill of a 100x100 polygon. 
        // This check only checks that it's above scale(3*100 + 2*50) + scaled_epsilon.
        // ok abs($paths->[0]->length - scale(3*100 + 2*50)) - scaled_epsilon, 'path has expected length';
        REQUIRE(std::abs(paths[0].length() - static_cast<double>(scale_(3*100 + 2*50))) - SCALED_EPSILON > 0); // path has expected length
    }

    SECTION("Rotated Square produces one continuous path") {
        Slic3r::ExPolygon expolygon(Polygon::new_scale({ {0, 0}, {50, 0}, {50, 50}, {0, 50} }));
        std::unique_ptr<Slic3r::Fill> filler(Slic3r::Fill::new_from_type("rectilinear"));
		filler->bounding_box = get_extents(expolygon);
        filler->angle = 0;
        
        Surface surface(stTop, expolygon);
        // width, height, nozzle_dmr
        auto flow = Slic3r::Flow(0.69f, 0.4f, 0.5f);

		FillParams fill_params;
        for (auto density : { 0.4, 1.0 }) {
            fill_params.density = density;
            filler->spacing = flow.spacing();
            REQUIRE(!fill_params.use_arachne); // Make this test fail when Arachne is used because this test is not ready for it.
            for (auto angle : { 0.0, 45.0}) {
                surface.expolygon.rotate(angle, Point(0,0));
                Polylines paths = filler->fill_surface(&surface, fill_params);
                // one continuous path
                REQUIRE(paths.size() == 1);
            }
        }
    }

    #if 0   // Disabled temporarily due to precission issues on the Mac VM
    SECTION("Solid surface fill") {
        Slic3r::Points points {
            Point::new_scale(6883102, 9598327.01296997),
            Point::new_scale(6883102, 20327272.01297),
            Point::new_scale(3116896, 20327272.01297),
            Point::new_scale(3116896, 9598327.01296997) 
        };
        Slic3r::ExPolygon expolygon(points);
         
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.55) == true);
        for (size_t i = 0; i <= 20; ++i)
        {
            expolygon.scale(1.05);
            REQUIRE(test_if_solid_surface_filled(expolygon, 0.55) == true);
        }
    }
    #endif

    SECTION("Solid surface fill") {
        Slic3r::Points points {
                Slic3r::Point(59515297,5422499),Slic3r::Point(59531249,5578697),Slic3r::Point(59695801,6123186),
                Slic3r::Point(59965713,6630228),Slic3r::Point(60328214,7070685),Slic3r::Point(60773285,7434379),
                Slic3r::Point(61274561,7702115),Slic3r::Point(61819378,7866770),Slic3r::Point(62390306,7924789),
                Slic3r::Point(62958700,7866744),Slic3r::Point(63503012,7702244),Slic3r::Point(64007365,7434357),
                Slic3r::Point(64449960,7070398),Slic3r::Point(64809327,6634999),Slic3r::Point(65082143,6123325),
                Slic3r::Point(65245005,5584454),Slic3r::Point(65266967,5422499),Slic3r::Point(66267307,5422499),
                Slic3r::Point(66269190,8310081),Slic3r::Point(66275379,17810072),Slic3r::Point(66277259,20697500),
                Slic3r::Point(65267237,20697500),Slic3r::Point(65245004,20533538),Slic3r::Point(65082082,19994444),
                Slic3r::Point(64811462,19488579),Slic3r::Point(64450624,19048208),Slic3r::Point(64012101,18686514),
                Slic3r::Point(63503122,18415781),Slic3r::Point(62959151,18251378),Slic3r::Point(62453416,18198442),
                Slic3r::Point(62390147,18197355),Slic3r::Point(62200087,18200576),Slic3r::Point(61813519,18252990),
                Slic3r::Point(61274433,18415918),Slic3r::Point(60768598,18686517),Slic3r::Point(60327567,19047892),
                Slic3r::Point(59963609,19493297),Slic3r::Point(59695865,19994587),Slic3r::Point(59531222,20539379),
                Slic3r::Point(59515153,20697500),Slic3r::Point(58502480,20697500),Slic3r::Point(58502480,5422499)
        };
        Slic3r::ExPolygon expolygon(points);
         
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.55) == true);
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.55, PI/2.0) == true);
    }
    SECTION("Solid surface fill") {
        Slic3r::Points points {
            Point::new_scale(0,0),Point::new_scale(98,0),Point::new_scale(98,10), Point::new_scale(0,10)
        };
        Slic3r::ExPolygon expolygon(points);
         
        REQUIRE(test_if_solid_surface_filled(expolygon, 0.5, 45.0, 0.99) == true);
    }
}

SCENARIO("Infill does not exceed perimeters", "[Fill]") 
{
    auto test = [](const std::string_view pattern) {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "nozzle_diameter",        "0.4, 0.4, 0.4, 0.4" },
            { "fill_pattern",           pattern },
            { "top_fill_pattern",       pattern },
            { "bottom_fill_pattern",    pattern },
            { "perimeters",             1 },
            { "skirts",                 0 },
            { "fill_density",           0.2 },
            { "layer_height",           0.05 },
            { "perimeter_extruder",     1 },
            { "infill_extruder",        2 }
        });
        
        WHEN("40mm cube sliced") {
            std::string gcode = Slic3r::Test::slice({ mesh(Slic3r::Test::TestMesh::cube_20x20x20, Vec3d::Zero(), 2.0) }, config);
            THEN("gcode not empty") {
                REQUIRE(! gcode.empty());
            }
            THEN("infill does not exceed perimeters") {
                GCodeReader parser;
                const int   perimeter_extruder = config.opt_int("perimeter_extruder");
                const int   infill_extruder    = config.opt_int("infill_extruder");
                int         tool = -1;
                Points      perimeter_points;
                Points      infill_points;
                parser.parse_buffer(gcode, [&tool, &perimeter_points, &infill_points, perimeter_extruder, infill_extruder]
                    (Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
                {
                    // if the command is a T command, set the the current tool
                    if (boost::starts_with(line.cmd(), "T")) {
                        tool = atoi(line.cmd().data() + 1) + 1;
                    } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
                        if (tool == perimeter_extruder)
                            perimeter_points.emplace_back(line.new_XY_scaled(self));
                        else if (tool == infill_extruder)
                            infill_points.emplace_back(line.new_XY_scaled(self));
                    }
                });
                auto convex_hull = Geometry::convex_hull(perimeter_points);
                int num_inside = std::count_if(infill_points.begin(), infill_points.end(), [&convex_hull](const Point &pt){ return convex_hull.contains(pt); });
                REQUIRE(num_inside == infill_points.size());
            }
        }
    };

    GIVEN("Rectilinear") { test("rectilinear"sv); }
    GIVEN("Honeycomb") { test("honeycomb"sv); }
    GIVEN("HilbertCurve") { test("hilbertcurve"sv); }
    GIVEN("Concentric") { test("concentric"sv); }
}

// SCENARIO("Infill only where needed", "[Fill]")
// {
//     DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
//     config.set_deserialize_strict({
//         { "nozzle_diameter",                "0.4, 0.4, 0.4, 0.4" },
//         { "infill_only_where_needed",       true },
//         { "bottom_solid_layers",            0 },
//         { "infill_extruder",                2 },
//         { "infill_extrusion_width",         0.5 },
//         { "wipe_into_infill",               false },
//         { "fill_density",                   0.4 },
//         // for preventing speeds from being altered
//         { "cooling",                        "0, 0, 0, 0" },
//         // for preventing speeds from being altered
//         { "first_layer_speed",              "100%" }
//     });

//     auto test = [&config]() -> double {
//         TriangleMesh pyramid = Test::mesh(Slic3r::Test::TestMesh::pyramid);
//         // Arachne doesn't use "Detect thin walls," and because of this, it filters out tiny infill areas differently.
//         // So, for Arachne, we cut the pyramid model to achieve similar results.
//         if (config.opt_enum<PerimeterGeneratorType>("perimeter_generator") == Slic3r::PerimeterGeneratorType::Arachne) {
//             indexed_triangle_set lower{};
//             cut_mesh(pyramid.its, 35, nullptr, &lower);
//             pyramid = TriangleMesh(lower);
//         }
//         std::string gcode = Slic3r::Test::slice({ pyramid }, config);
//         THEN("gcode not empty") {
//             REQUIRE(! gcode.empty());
//         }

//         GCodeReader parser;
//         int         tool = -1;
//         const int   infill_extruder = config.opt_int("infill_extruder");
//         Points      infill_points;
//         parser.parse_buffer(gcode, [&tool, &infill_points, infill_extruder](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
//         {
//             // if the command is a T command, set the the current tool
//             if (boost::starts_with(line.cmd(), "T")) {
//                 tool = atoi(line.cmd().data() + 1) + 1;
//             } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
//                 if (tool == infill_extruder) {
//                     infill_points.emplace_back(self.xy_scaled());
//                     infill_points.emplace_back(line.new_XY_scaled(self));
//                 }
//             }
//         });
//         // prevent calling convex_hull() with no points
//         THEN("infill not empty") {
//             REQUIRE(! infill_points.empty());
//         }

//         auto opt_width = config.opt<ConfigOptionFloatOrPercent>("infill_extrusion_width");
//         REQUIRE(! opt_width->percent);
//         Polygons convex_hull = expand(Geometry::convex_hull(infill_points), scaled<float>(opt_width->value / 2));
//         return SCALING_FACTOR * SCALING_FACTOR * std::accumulate(convex_hull.begin(), convex_hull.end(), 0., [](double acc, const Polygon &poly){ return acc + poly.area(); });
//     };

//     double tolerance = 5; // mm^2
    
//     // GIVEN("solid_infill_below_area == 0") {
//     //     config.opt_float("solid_infill_below_area") = 0;
//     //     WHEN("pyramid is sliced ") {
//     //         auto area = test();
//     //         THEN("no infill is generated when using infill_only_where_needed on a pyramid") {
//     //             REQUIRE(area < tolerance);
//     //         }
//     //     }
//     // }
//     // GIVEN("solid_infill_below_area == 70") {
//     //     config.opt_float("solid_infill_below_area") = 70;
//     //     WHEN("pyramid is sliced ") {
//     //         auto area = test();
//     //         THEN("infill is only generated under the forced solid shells") {
//     //             REQUIRE(std::abs(area - 70) < tolerance);
//     //         }
//     //     }
//     // }
// }

SCENARIO("Combine infill", "[Fill]")
{
    {
        auto test = [](const DynamicPrintConfig &config) {
            std::string gcode = Test::slice({ Test::TestMesh::cube_20x20x20 }, config);
            THEN("infill_every_layers does not crash") {
                REQUIRE(! gcode.empty());
            }

            Slic3r::GCodeReader parser;
            int tool = -1;
            std::set<coord_t> layers; // layer_z => 1
            std::map<coord_t, bool> layer_infill; // layer_z => has_infill
            const int infill_extruder           = config.opt_int("infill_extruder");
            const int support_material_extruder = config.opt_int("support_material_extruder");
            parser.parse_buffer(gcode,
                [&tool, &layers, &layer_infill, infill_extruder, support_material_extruder](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line)
            {
                coord_t z = line.new_Z(self) / SCALING_FACTOR;
                if (boost::starts_with(line.cmd(), "T")) {
                    tool = atoi(line.cmd().data() + 1);
                } else if (line.cmd_is("G1") && line.extruding(self) && line.dist_XY(self) > 0 && tool + 1 != support_material_extruder) {
                    if (tool + 1 == infill_extruder)
                        layer_infill[z] = true;
                    else if (auto it = layer_infill.find(z); it == layer_infill.end())
                        layer_infill.insert(it, std::make_pair(z, false));
                }
                // Previously, all G-code commands had a fixed number of decimal points with means with redundant zeros after decimal points.
                // We changed this behavior and got rid of these redundant padding zeros, which caused this test to fail
                // because the position in Z-axis is compared as a string, and previously, G-code contained the following two commands:
                // "G1 Z5 F5000 ; lift nozzle"
                // "G1 Z5.000 F7800.000"
                // That has a different Z-axis position from the view of string comparisons of floating-point numbers.
                // To correct the computation of the number of printed layers, even in the case of string comparisons of floating-point numbers,
                // we filtered out the G-code command with the commend 'lift nozzle'.
                if (line.cmd_is("G1") && line.dist_Z(self) != 0 && line.comment().find("lift nozzle") == std::string::npos)
                    layers.insert(z);
            });
            
            auto layers_with_perimeters = int(layer_infill.size());
            auto layers_with_infill     = int(std::count_if(layer_infill.begin(), layer_infill.end(), [](auto &v){ return v.second; }));
            THEN("expected number of layers") {
                REQUIRE(layers.size() == layers_with_perimeters + config.opt_int("raft_layers"));
            }
            
            if (config.opt_int("raft_layers") == 0) {
                // first infill layer printed directly on print bed is not combined, so we don't consider it.
                -- layers_with_infill;
                -- layers_with_perimeters;
            }
            
            // we expect that infill is generated for half the number of combined layers
            // plus for each single layer that was not combined (remainder)
            THEN("infill is only present in correct number of layers") {
                int infill_every = config.opt_int("infill_every_layers");
                REQUIRE(layers_with_infill == int(layers_with_perimeters / infill_every) + (layers_with_perimeters % infill_every));
            }
        };
        
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "nozzle_diameter",        "0.5, 0.5, 0.5, 0.5" },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "infill_every_layers",    2  },
            { "perimeter_extruder",     1 },
            { "infill_extruder",        2 },
            { "wipe_into_infill",       false },
            { "support_material_extruder", 3 },
            { "support_material_interface_extruder", 3 },
            { "top_solid_layers",       0 },
            { "bottom_solid_layers",    0 }
        });

        test(config);

        // Reuse the config above
        config.set_deserialize_strict({
            { "skirts", 0 }, // prevent usage of perimeter_extruder in raft layers
            { "raft_layers", 5 }
        });
        test(config);
    }

    WHEN("infill_every_layers == 2") {
        Slic3r::Print print;
        Slic3r::Test::init_and_process_print({ Test::TestMesh::cube_20x20x20 }, print, {
            { "nozzle_diameter",        "0.5" },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "infill_every_layers",    2  }
        });        
        THEN("infill combination produces internal void surfaces") {
            bool has_void = false;
            for (const Layer *layer : print.get_object(0)->layers())
                if (layer->get_region(0)->fill_surfaces().filter_by_type(stInternalVoid).size() > 0) {
                    has_void = true;
                    break;
                }
            REQUIRE(has_void);
        }
    }
        
    WHEN("infill_every_layers disabled") {
        // we disable combination after infill has been generated
        Slic3r::Print print;
        Slic3r::Test::init_and_process_print({ Test::TestMesh::cube_20x20x20 }, print, {
            { "nozzle_diameter",        "0.5" },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "infill_every_layers",    1  }
        });        

        THEN("infill combination is idempotent") {
            bool has_infill_on_each_layer = true;
            for (const Layer *layer : print.get_object(0)->layers())
                if (layer->get_region(0)->fill_surfaces().empty()) {
                    has_infill_on_each_layer = false;
                    break;
                }
            REQUIRE(has_infill_on_each_layer);
        }
    }
}

SCENARIO("Infill density zero", "[Fill]")
{
    WHEN("20mm cube is sliced") {
        DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict({
            { "skirts",                         0 },
            { "perimeters",                     1 },
            { "fill_density",                   0 },
            { "top_solid_layers",               0 },
            { "bottom_solid_layers",            0 },
            { "solid_infill_below_area",        20000000 },
            { "solid_infill_every_layers",      2 },
            { "perimeter_speed",                99 },
            { "external_perimeter_speed",       99 },
            { "cooling",                        "0" },
            { "first_layer_speed",              "100%" }
        });

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);
        THEN("gcode not empty") {
            REQUIRE(! gcode.empty());
        }

        THEN("solid_infill_below_area and solid_infill_every_layers are ignored when fill_density is 0") {
            GCodeReader  parser;
            const double perimeter_speed = config.opt_float("perimeter_speed");
            std::map<double, double> layers_with_extrusion;
            parser.parse_buffer(gcode, [&layers_with_extrusion, perimeter_speed](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
                if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
                    double f = line.new_F(self);
                    if (std::abs(f - perimeter_speed * 60.) > 0.01)
                        // It is a perimeter.
                        layers_with_extrusion[self.z()] = f;
                }
            });
            REQUIRE(layers_with_extrusion.empty());
        }
    }

    WHEN("A is sliced") {
        DynamicPrintConfig config = Slic3r::DynamicPrintConfig::full_print_config();
        config.set_deserialize_strict({
            { "skirts",                         0 },
            { "perimeters",                     3 },
            { "fill_density",                   0 },
            { "layer_height",                   0.2 },
            { "first_layer_height",             0.2 },
            { "nozzle_diameter",                "0.35,0.35,0.35,0.35" },
            { "infill_extruder",                2 },
            { "solid_infill_extruder",          2 },
            { "infill_extrusion_width",         0.52 },
            { "solid_infill_extrusion_width",   0.52 },
            { "first_layer_extrusion_width",    0 }
        });

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::A }, config);
        THEN("gcode not empty") {
            REQUIRE(! gcode.empty());
        }

        THEN("no missing parts in solid shell when fill_density is 0") {
            GCodeReader  parser;
            int          tool = -1;
            const int    infill_extruder = config.opt_int("infill_extruder");
            std::map<coord_t, Lines> infill;
            parser.parse_buffer(gcode, [&tool, &infill, infill_extruder](Slic3r::GCodeReader &self, const Slic3r::GCodeReader::GCodeLine &line) {
                if (boost::starts_with(line.cmd(), "T")) {
                    tool = atoi(line.cmd().data() + 1) + 1;
                } else if (line.cmd() == "G1" && line.extruding(self) && line.dist_XY(self) > 0) {
                    if (tool == infill_extruder)
                        infill[scaled<coord_t>(self.z())].emplace_back(self.xy_scaled(), line.new_XY_scaled(self));
                }
            });
            auto opt_width = config.opt<ConfigOptionFloatOrPercent>("infill_extrusion_width");
            REQUIRE(! opt_width->percent);
            auto grow_d = scaled<float>(opt_width->value / 2);
            auto inflate_lines = [grow_d](const Lines &lines) {
                Polygons out;
                for (const Line &line : lines)
                    append(out, offset(Polyline{ line.a, line.b }, grow_d, Slic3r::ClipperLib::jtSquare, 3.));
                return union_(out);
            };
            Polygons     layer0_infill = inflate_lines(infill[scaled<coord_t>(0.2)]);
            Polygons     layer1_infill = inflate_lines(infill[scaled<coord_t>(0.4)]);
            ExPolygons   poly          = opening_ex(diff_ex(layer0_infill, layer1_infill), grow_d);
            const double threshold     = 2. * sqr(grow_d * 2.);
            int          missing_parts = std::count_if(poly.begin(), poly.end(), [threshold](const ExPolygon &poly){ return poly.area() > threshold; });
            REQUIRE(missing_parts == 0);
        }
    }
}

/*
{
    # GH: #2697
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('perimeter_extrusion_width', 0.72);
    $config->set('top_infill_extrusion_width', 0.1);
    $config->set('infill_extruder', 2);         # in order to distinguish infill
        $config->set('solid_infill_extruder', 2);   # in order to distinguish infill

        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my %infill = ();  # Z => [ Line, Line ... ]
        my %other  = ();  # Z => [ Line, Line ... ]
        my $tool = undef;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;

            if ($cmd =~ /^T(\d+)/) {
            $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            my $z = 1 * $self->Z;
            my $line = Slic3r::Line->new_scale(
                    [ $self->X, $self->Y ],
                    [ $info->{new_X}, $info->{new_Y} ],
                    );
            if ($tool == $config->infill_extruder-1) {
            $infill{$z} //= [];
            push @{$infill{$z}}, $line;
            } else {
            $other{$z} //= [];
            push @{$other{$z}}, $line;
            }
            }
            });
    my $top_z = max(keys %infill);
    my $top_infill_grow_d = scale($config->top_infill_extrusion_width)/2;
    my $top_infill = union([ map @{$_->grow($top_infill_grow_d)}, @{ $infill{$top_z} } ]);
    my $perimeters_grow_d = scale($config->perimeter_extrusion_width)/2;
    my $perimeters = union([ map @{$_->grow($perimeters_grow_d)}, @{ $other{$top_z} } ]);
    my $covered = union_ex([ @$top_infill, @$perimeters ]);
    my @holes = map @{$_->holes}, @$covered;
    ok sum(map unscale unscale $_->area*-1, @holes) < 1, 'no gaps between top solid infill and perimeters';
}

{
    skip "The FillRectilinear2 does not fill the surface completely", 1;

    my $test = sub {
        my ($expolygon, $flow_spacing, $angle, $density) = @_;
        
        my $filler = Slic3r::Filler->new_from_type('rectilinear');
        $filler->set_bounding_box($expolygon->bounding_box);
        $filler->set_angle($angle // 0);
        # Adjust line spacing to fill the region.
        $filler->set_dont_adjust(0);
        $filler->set_link_max_length(scale(1.2*$flow_spacing));
        my $surface = Slic3r::Surface->new(
            surface_type    => S_TYPE_BOTTOM,
            expolygon       => $expolygon,
        );
        my $flow = Slic3r::Flow->new(
            width           => $flow_spacing,
            height          => 0.4,
            nozzle_diameter => $flow_spacing,
        );
        $filler->set_spacing($flow->spacing);
        my $paths = $filler->fill_surface(
            $surface,
            layer_height    => $flow->height,
            density         => $density // 1,
        );
        
        # check whether any part was left uncovered
        my @grown_paths = map @{Slic3r::Polyline->new(@$_)->grow(scale $filler->spacing/2)}, @$paths;
        my $uncovered = diff_ex([ @$expolygon ], [ @grown_paths ], 1);
        
        # ignore very small dots
        my $uncovered_filtered = [ grep $_->area > (scale $flow_spacing)**2, @$uncovered ];

        is scalar(@$uncovered_filtered), 0, 'solid surface is fully filled';
        
        if (0 && @$uncovered_filtered) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("uncovered.svg", 
                no_arrows       => 1,
                expolygons      => [ $expolygon ],
                blue_expolygons => [ @$uncovered ],
                red_expolygons  => [ @$uncovered_filtered ],
                polylines       => [ @$paths ],
            );
            exit;
        }
    };
    
    my $expolygon = Slic3r::ExPolygon->new([
        [6883102, 9598327.01296997],
        [6883102, 20327272.01297],
        [3116896, 20327272.01297],
        [3116896, 9598327.01296997],
    ]);
    $test->($expolygon, 0.55);
    
    for (1..20) {
        $expolygon->scale(1.05);
        $test->($expolygon, 0.55);
    }
    
    $expolygon = Slic3r::ExPolygon->new(
        [[59515297,5422499],[59531249,5578697],[59695801,6123186],[59965713,6630228],[60328214,7070685],[60773285,7434379],[61274561,7702115],[61819378,7866770],[62390306,7924789],[62958700,7866744],[63503012,7702244],[64007365,7434357],[64449960,7070398],[64809327,6634999],[65082143,6123325],[65245005,5584454],[65266967,5422499],[66267307,5422499],[66269190,8310081],[66275379,17810072],[66277259,20697500],[65267237,20697500],[65245004,20533538],[65082082,19994444],[64811462,19488579],[64450624,19048208],[64012101,18686514],[63503122,18415781],[62959151,18251378],[62453416,18198442],[62390147,18197355],[62200087,18200576],[61813519,18252990],[61274433,18415918],[60768598,18686517],[60327567,19047892],[59963609,19493297],[59695865,19994587],[59531222,20539379],[59515153,20697500],[58502480,20697500],[58502480,5422499]]
    );
    $test->($expolygon, 0.524341649025257);
    
    $expolygon = Slic3r::ExPolygon->new([ scale_points [0,0], [98,0], [98,10], [0,10] ]);
    $test->($expolygon, 0.5, 45, 0.99);  # non-solid infill
}
*/

bool test_if_solid_surface_filled(const ExPolygon& expolygon, double flow_spacing, double angle, double density)
{
    std::unique_ptr<Slic3r::Fill> filler(Slic3r::Fill::new_from_type("rectilinear"));
	filler->bounding_box = get_extents(expolygon.contour);
    filler->angle = float(angle);

	Flow flow(float(flow_spacing), 0.4f, float(flow_spacing));
	filler->spacing = flow.spacing();

	FillParams fill_params;
	fill_params.density = float(density);
	fill_params.dont_adjust = false;

    Surface surface(stBottom, expolygon);
    if (fill_params.use_arachne) // Make this test fail when Arachne is used because this test is not ready for it.
        return false;
    Slic3r::Polylines paths = filler->fill_surface(&surface, fill_params);

    // check whether any part was left uncovered
    Polygons grown_paths;
    grown_paths.reserve(paths.size());

    // figure out what is actually going on here re: data types
    float line_offset = float(scale_(filler->spacing / 2.0 + EPSILON));
    std::for_each(paths.begin(), paths.end(), [line_offset, &grown_paths] (const Slic3r::Polyline& p) {
        polygons_append(grown_paths, offset(p, line_offset));
    });

	// Shrink the initial expolygon a bit, this simulates the infill / perimeter overlap that we usually apply.
    ExPolygons uncovered = diff_ex(offset(expolygon, - float(0.2 * scale_(flow_spacing))), grown_paths, ApplySafetyOffset::Yes);

    // ignore very small dots
    const double scaled_flow_spacing = std::pow(scale_(flow_spacing), 2);
    uncovered.erase(std::remove_if(uncovered.begin(), uncovered.end(), [scaled_flow_spacing](const ExPolygon& poly) { return poly.area() < scaled_flow_spacing; }), uncovered.end());

#if 0
	if (! uncovered.empty()) {
		BoundingBox bbox = get_extents(expolygon.contour);
		bbox.merge(get_extents(uncovered));
		bbox.merge(get_extents(grown_paths));
		SVG svg("c:\\data\\temp\\test_if_solid_surface_filled.svg", bbox);
		svg.draw(expolygon);
		svg.draw(uncovered, "red");
		svg.Close();
	}
#endif

    return uncovered.empty(); // solid surface is fully filled
}
