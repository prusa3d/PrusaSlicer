#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/GCodeReader/GCodeReader.hpp"

#include "test_data.hpp" // get access to init_print, etc

using namespace Slic3r::Test;
using namespace Slic3r;
using Biz::GCodeReader::GCodeReader;
using Domain::FloatOrPercentage;
using Domain::Percentage;

SCENARIO("Shells", "[Shells]") {
    GIVEN("20mm box") {
        auto test = [&](const TestConfig &config){
            std::vector<coord_t> zs;
            std::set<coord_t> layers_with_solid_infill;
            std::set<coord_t> layers_with_bridge_infill;
            const double solid_infill_speed = config.print.items.opt("solid_infill_speed").get<FloatOrPercentage>().float_value() * 60;
            const double bridge_speed       = config.print.items.opt("bridge_speed").get<double>() * 60;
            GCodeReader parser;
            parser.parse_buffer(Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config),
                [&zs, &layers_with_solid_infill, &layers_with_bridge_infill, solid_infill_speed, bridge_speed]
                    (GCodeReader &self, const GCodeReader::GCodeLine &line)
            {
                double z = line.new_Z(self);
                REQUIRE(z >= 0);
                if (z > 0) {
                    coord_t scaled_z = scaled<float>(z);
                    zs.emplace_back(scaled_z);
                    if (line.extruding(self) && line.dist_XY(self) > 0) {
                        double f = line.new_F(self);
                        if (std::abs(f - solid_infill_speed) < EPSILON)
                            layers_with_solid_infill.insert(scaled_z);
                        if (std::abs(f - bridge_speed) < EPSILON)
                            layers_with_bridge_infill.insert(scaled_z);
                    }
                }
            });
            sort_remove_duplicates(zs);

            auto has_solid_infill  = [&layers_with_solid_infill](coord_t z) { return layers_with_solid_infill.find(z) != layers_with_solid_infill.end(); };
            auto has_bridge_infill = [&layers_with_bridge_infill](coord_t z) { return layers_with_bridge_infill.find(z) != layers_with_bridge_infill.end(); };
            auto has_shells        = [&has_solid_infill, &has_bridge_infill, &zs](int layer_idx) { coord_t z = zs[layer_idx]; return has_solid_infill(z) || has_bridge_infill(z); };
            const int bottom_solid_layers = config.print.items.opt("bottom_solid_layers").get<int>();
            const int top_solid_layers    = config.print.items.opt("top_solid_layers").get<int>();
            THEN("correct number of bottom solid layers") {
                for (int i = 0; i < bottom_solid_layers; ++ i)
                    REQUIRE(has_shells(i));
                for (int i = bottom_solid_layers; i < int(zs.size() / 2); ++ i)
                    REQUIRE(! has_shells(i));
            }
            THEN("correct number of top solid layers") {
                // NOTE: there is one additional layer with enusring line under the bridge layer, bridges would be otherwise anchored weakly to the perimeter.
                size_t additional_ensuring_anchors = top_solid_layers > 0 ? 1 : 0;
                for (int i = 0; i < top_solid_layers + additional_ensuring_anchors; ++ i)
                    REQUIRE(has_shells(int(zs.size()) - i - 1));
                for (int i = top_solid_layers + additional_ensuring_anchors; i < int(zs.size() / 2); ++ i)
                    REQUIRE(! has_shells(int(zs.size()) - i - 1));
            }
            if (top_solid_layers > 0) {
                THEN("solid infill speed is used on solid infill") {
                    for (int i = 0; i < top_solid_layers - 1; ++ i) {
                        auto z = zs[int(zs.size()) - i - 1];
                        REQUIRE(has_solid_infill(z));
                        REQUIRE(! has_bridge_infill(z));
                    }
                }
                THEN("bridge used in first solid layer over sparse infill") {
                    auto z = zs[int(zs.size()) - top_solid_layers];
                    REQUIRE(! has_solid_infill(z));
                    REQUIRE(has_bridge_infill(z));
                }
            }
        };

        TestConfig config;
        config.print.items.opt("skirts").set(0);
        config.print.items.opt("perimeters").set(0);
        config.print.items.opt("solid_infill_speed").set(FloatOrPercentage{99});
        config.print.items.opt("top_solid_infill_speed").set(FloatOrPercentage{99});
        config.print.items.opt("bridge_speed").set(72.0);
        config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});
        config.filament.at(0).items.opt("cooling").set(false);

        WHEN("three top and bottom layers") {
            // proper number of shells is applied
            config.print.items.opt("top_solid_layers").set(3);
            config.print.items.opt("bottom_solid_layers").set(3);
            test(config);
        }

        WHEN("zero top and bottom layers") {
            // no shells are applied when both top and bottom are set to zero
            config.print.items.opt("top_solid_layers").set(0);
            config.print.items.opt("bottom_solid_layers").set(0);
            test(config);
        }

        WHEN("three top and bottom layers, zero infill") {
            // proper number of shells is applied even when fill density is none
            config.print.items.opt("perimeters").set(1);
            config.print.items.opt("top_solid_layers").set(3);
            config.print.items.opt("bottom_solid_layers").set(3);
            test(config);
        }
    }
}

static std::set<double> layers_with_speed(const std::string &gcode, int speed)
{
    std::set<double> out;
    GCodeReader parser;
    parser.parse_buffer(gcode, [&out, speed](GCodeReader &self, const GCodeReader::GCodeLine &line) {
        if (line.extruding(self) && is_approx<double>(line.new_F(self), speed * 60.))
            out.insert(self.z());
    });
    return out;
}

SCENARIO("Shells (from Perl)", "[Shells]") {
    GIVEN("V shape, Slic3r GH #1161") {
        double solid_speed = 99.0;

        TestConfig config;
        config.print.items.opt("layer_height").set(0.3);
        config.print.items.opt("first_layer_height").set(FloatOrPercentage{0.3});
        config.print.items.opt("bottom_solid_layers").set(0);
        config.print.items.opt("top_solid_layers").set(3);
        // to prevent speeds from being altered
        config.filament.at(0).items.opt("cooling").set(false);
        config.print.items.opt("bridge_speed").set(solid_speed);
        config.print.items.opt("solid_infill_speed").set(FloatOrPercentage{solid_speed});
        config.print.items.opt("top_solid_infill_speed").set(FloatOrPercentage{solid_speed});
        // to prevent speeds from being altered
        config.print.items.opt("first_layer_speed").set(FloatOrPercentage{Percentage{100}});
        // prevent speed alteration
        config.print.items.opt("enable_dynamic_overhang_speeds").set(false);

        THEN("correct number of top solid shells is generated in V-shaped object") {
            size_t n = 0;
            for (auto z : layers_with_speed(Slic3r::Test::slice({TestMesh::V}, config), solid_speed))
                if (z <= 7.2)
                    ++ n;
            REQUIRE(n == 3 + 1/*one additional layer with ensuring for bridge anchors*/);
        }
    }

    GIVEN("20mm_cube, spiral vase") {
        double layer_height = 0.3;
        TestConfig config;
        config.print.items.opt("perimeters").set(1);
        config.print.items.opt("fill_density").set(Percentage{0});
        config.print.items.opt("layer_height").set(layer_height);
        config.print.items.opt("first_layer_height").set(FloatOrPercentage{layer_height});
        config.print.items.opt("top_solid_layers").set(0 );
        config.print.items.opt("spiral_vase").set(true);
        config.print.items.opt("bottom_solid_layers").set(0 );
        config.print.items.opt("skirts").set(0 );
        config.printer.items.opt("start_gcode").set("" );
        config.filament.at(0).items.opt("temperature").set(200);
        config.filament.at(0).items.opt("first_layer_temperature").set(205);

        // TODO: this needs to be tested with a model with sloping edges, where starting
        // points of each layer are not aligned - in that case we would test that no
        // travel moves are left to move to the new starting point - in a cube, end
        // points coincide with next layer starting points (provided there's no clipping)
        auto test = [&](const TestConfig &config) {
            size_t              travel_moves_after_first_extrusion  = 0;
            bool                started_extruding                   = false;
            bool                first_layer_temperature_set         = false;
            bool                temperature_set                     = false;
            std::vector<double> z_steps;
            GCodeReader         parser;
            parser.parse_buffer(Slic3r::Test::slice({TestMesh::cube_20x20x20}, config), 
                [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
                if (line.cmd_is("G1")) {
                    if (line.extruding(self))
                        started_extruding = true;
                    if (started_extruding) {
                        if (double dz = line.dist_Z(self); dz > 0)
                            z_steps.emplace_back(dz);
                        if (line.travel() && line.dist_XY(self) > 0 && ! line.has(Z))
                            ++ travel_moves_after_first_extrusion;
                    }
                } else if (line.cmd_is("M104")) {
                    int s;
                    if (line.has_value('S', s)) {
                        if (s == 205)
                            first_layer_temperature_set = true;
                        else if (s == 200)
                            temperature_set = true;
                    }
                }
            });
            THEN("first layer temperature is set") {
                REQUIRE(first_layer_temperature_set);
            }
            THEN("temperature is set") {
                REQUIRE(temperature_set);
            }
            // we allow one travel move after first extrusion: i.e. when moving to the first
            // spiral point after moving to second layer (bottom layer had loop clipping, so
            // we're slightly distant from the starting point of the loop)
            THEN("no gaps in spiral vase") {
                REQUIRE(travel_moves_after_first_extrusion <= 1);
            }
            THEN("no gaps in Z") {
                REQUIRE(std::count_if(z_steps.begin(), z_steps.end(), 
                    [&layer_height](auto z_step) { return z_step > layer_height + EPSILON; }) == 0);
            }
        };
        WHEN("solid model") {
            test(config);
        }
        WHEN("solid model with negative z-offset") {
            config.printer.items.opt("z_offset").set(-10.0);
            test(config);
        }
        // Disabled because the current unreliable medial axis code doesn't always produce valid loops.
        // $test->('40x10', 'hollow model with negative z-offset');
    }
    GIVEN("20mm_cube, spiral vase") {
        double layer_height = 0.4;

        TestConfig config;
        config.print.items.opt("spiral_vase").set(true);
        config.print.items.opt("perimeters").set(1);
        config.print.items.opt("fill_density").set(Percentage{0});
        config.print.items.opt("top_solid_layers").set(0);
        config.print.items.opt("bottom_solid_layers").set(0);
        config.print.items.opt("retract_layer_change").set(false);
        config.print.items.opt("skirts").set(0);
        config.print.items.opt("layer_height").set(layer_height);
        config.print.items.opt("first_layer_height").set(FloatOrPercentage{layer_height});
        config.printer.items.opt("start_gcode").set("" );
            // { "use_relative_e_distances", 1}

        std::vector<std::pair<double, double>> this_layer; // [ dist_Z, dist_XY ], ...
        int  z_moves                                    = 0;
        bool bottom_layer_not_flat                      = false;
        bool null_z_moves_not_layer_changes             = false;
        bool null_z_moves_not_multiples_of_layer_height = false;
        bool sum_of_partial_z_equals_to_layer_height    = false;
        bool all_layer_segments_have_same_slope         = false;
        bool horizontal_extrusions                      = false;
        GCodeReader parser;
        parser.parse_buffer(Slic3r::Test::slice({TestMesh::cube_20x20x20}, config), 
            [&](GCodeReader &self, const GCodeReader::GCodeLine &line) {
            if (line.cmd_is("G1")) {
                if (z_moves < 2) {
                    // skip everything up to the second Z move
                    // (i.e. start of second layer)
                    if (line.has(Z)) {
                        ++ z_moves;
                        if (double dz = line.dist_Z(self); dz > 0 && ! is_approx<double>(dz, layer_height))
                            bottom_layer_not_flat = true;
                    }
                } else if (line.dist_Z(self) == 0 && line.has(Z)) {
                    if (line.dist_XY(self) != 0)
                        null_z_moves_not_layer_changes = true;
                    double z = line.new_Z(self);
                    if (fmod(z + EPSILON, layer_height) > 2 * EPSILON)
                        null_z_moves_not_multiples_of_layer_height = true;
                    double total_dist_XY = 0;
                    double total_dist_Z  = 0;
                    for (auto &seg : this_layer) {
                        total_dist_Z  += seg.first;
                        total_dist_XY += seg.second;
                    }
                    if (std::abs(total_dist_Z - layer_height) >
                            // The first segment on the 2nd layer has extrusion interpolated from zero 
                            // and the 1st segment has such a low extrusion assigned, that it is effectively zero, thus the move
                            // is considered non-extruding and a higher epsilon is required.
                            (z_moves == 2 ? 0.0021 : EPSILON))
                        sum_of_partial_z_equals_to_layer_height = true;
                    //printf("Total height: %f, layer height: %f, good: %d\n", sum(map $_->[0], @this_layer), $config->layer_height, $sum_of_partial_z_equals_to_layer_height);
                    for (auto &seg : this_layer)
                        // check that segment's dist_Z is proportioned to its dist_XY
                        if (std::abs(seg.first * total_dist_XY / layer_height - seg.second) > 0.2)
                            all_layer_segments_have_same_slope = true;
                    this_layer.clear();
                } else if (line.extruding(self) && line.dist_XY(self) > 0) {
                    if (line.dist_Z(self) == 0)
                        horizontal_extrusions = true;
                    //printf("Pushing dist_z: %f, dist_xy: %f\n", $info->{dist_Z}, $info->{dist_XY});
                    this_layer.emplace_back(line.dist_Z(self), line.dist_XY(self));
                }
            }
        });
        THEN("bottom layer is flat when using spiral vase") {
            REQUIRE(! bottom_layer_not_flat);
        }
        THEN("null Z moves are layer changes") {
            REQUIRE(! null_z_moves_not_layer_changes);
        }
        THEN("null Z moves are multiples of layer height") {
            REQUIRE(! null_z_moves_not_multiples_of_layer_height);
        }
        THEN("sum of partial Z increments equals to a full layer height") {
            REQUIRE(! sum_of_partial_z_equals_to_layer_height);
        }
        THEN("all layer segments have the same slope") {
            REQUIRE(! all_layer_segments_have_same_slope);
        }
        THEN("no horizontal extrusions") {
            REQUIRE(! horizontal_extrusions);
        }
    }
}

#if 0
// The current Spiral Vase slicing code removes the holes and all but the largest contours from each slice,
// therefore the following test is no more valid.
{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('perimeters', 1);
    $config->set('fill_density', 0);
    $config->set('top_solid_layers', 0);
    $config->set('spiral_vase', 1);
    $config->set('bottom_solid_layers', 0);
    $config->set('skirts', 0);
    $config->set('first_layer_height', $config->layer_height);
    $config->set('start_gcode', '');
    
    my $print = Slic3r::Test::init_print('two_hollow_squares', config => $config);
    my $diagonal_moves = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1') {
            if ($info->{extruding} && $info->{dist_XY} > 0) {
                if ($info->{dist_Z} > 0) {
                    $diagonal_moves++;
                }
            }
        }
    });
    is $diagonal_moves, 0, 'no spiral moves on two-island object';
}
#endif
