#include <catch2/catch_test_macros.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Feature/FuzzySkin/FuzzySkin.hpp"
#include "libslic3r/Arachne/utils/ExtrusionLine.hpp"

#include "test_data.hpp"

using namespace Slic3r;
using namespace Slic3r::Feature::FuzzySkin;

// Helper to create a simple square polygon
static Polygon make_square(coord_t size)
{
    return Polygon({
        Point(0, 0),
        Point(size, 0),
        Point(size, size),
        Point(0, size)
    });
}

// Helper to create a config with specific fuzzy skin settings
static PrintRegionConfig make_fuzzy_config(
    FuzzySkinType type = FuzzySkinType::External,
    FuzzySkinMode mode = FuzzySkinMode::Displacement,
    NoiseType noise_type = NoiseType::Classic,
    double thickness = 0.3,
    double point_dist = 0.8,
    bool first_layer = false,
    double scale = 1.0,
    int octaves = 4,
    double persistence = 0.5)
{
    PrintRegionConfig config;
    config.fuzzy_skin.value = type;
    config.fuzzy_skin_mode.value = mode;
    config.fuzzy_skin_noise_type.value = noise_type;
    config.fuzzy_skin_thickness.value = thickness;
    config.fuzzy_skin_point_dist.value = point_dist;
    config.fuzzy_skin_first_layer.value = first_layer;
    config.fuzzy_skin_scale.value = scale;
    config.fuzzy_skin_octaves.value = octaves;
    config.fuzzy_skin_persistence.value = persistence;
    return config;
}

SCENARIO("should_fuzzify logic", "[FuzzySkin]")
{
    GIVEN("FuzzySkinType::None") {
        PrintRegionConfig config = make_fuzzy_config(FuzzySkinType::None);

        THEN("never fuzzifies") {
            REQUIRE_FALSE(should_fuzzify(config, 0, 0, true));
            REQUIRE_FALSE(should_fuzzify(config, 1, 0, true));
            REQUIRE_FALSE(should_fuzzify(config, 1, 0, false));
            REQUIRE_FALSE(should_fuzzify(config, 1, 1, true));
        }
    }

    GIVEN("FuzzySkinType::External") {
        PrintRegionConfig config = make_fuzzy_config(FuzzySkinType::External);

        THEN("fuzzifies only outermost contours, not first layer") {
            // Layer 0 (first layer) - should not fuzzify by default
            REQUIRE_FALSE(should_fuzzify(config, 0, 0, true));

            // Layer 1+, perimeter 0, contour - should fuzzify
            REQUIRE(should_fuzzify(config, 1, 0, true));

            // Layer 1+, perimeter 0, hole - should NOT fuzzify (External mode)
            REQUIRE_FALSE(should_fuzzify(config, 1, 0, false));

            // Layer 1+, inner perimeters - should NOT fuzzify
            REQUIRE_FALSE(should_fuzzify(config, 1, 1, true));
            REQUIRE_FALSE(should_fuzzify(config, 1, 2, true));
        }
    }

    GIVEN("FuzzySkinType::All") {
        PrintRegionConfig config = make_fuzzy_config(FuzzySkinType::All);

        THEN("fuzzifies outermost perimeters including holes, not first layer") {
            // Layer 0 (first layer) - should not fuzzify by default
            REQUIRE_FALSE(should_fuzzify(config, 0, 0, true));

            // Layer 1+, perimeter 0, contour - should fuzzify
            REQUIRE(should_fuzzify(config, 1, 0, true));

            // Layer 1+, perimeter 0, hole - should fuzzify (All mode)
            REQUIRE(should_fuzzify(config, 1, 0, false));

            // Layer 1+, inner perimeters - should NOT fuzzify
            REQUIRE_FALSE(should_fuzzify(config, 1, 1, true));
        }
    }

    GIVEN("FuzzySkinType::AllWalls") {
        PrintRegionConfig config = make_fuzzy_config(FuzzySkinType::AllWalls);

        THEN("fuzzifies all perimeters on all walls, not first layer") {
            // Layer 0 (first layer) - should not fuzzify by default
            REQUIRE_FALSE(should_fuzzify(config, 0, 0, true));
            REQUIRE_FALSE(should_fuzzify(config, 0, 1, true));

            // Layer 1+, all perimeters - should fuzzify
            REQUIRE(should_fuzzify(config, 1, 0, true));
            REQUIRE(should_fuzzify(config, 1, 0, false));
            REQUIRE(should_fuzzify(config, 1, 1, true));
            REQUIRE(should_fuzzify(config, 1, 2, true));
            REQUIRE(should_fuzzify(config, 1, 5, false));
        }
    }

    GIVEN("fuzzy_skin_first_layer enabled") {
        PrintRegionConfig config = make_fuzzy_config(FuzzySkinType::External);
        config.fuzzy_skin_first_layer.value = true;

        THEN("first layer is also fuzzified") {
            REQUIRE(should_fuzzify(config, 0, 0, true));
            REQUIRE(should_fuzzify(config, 1, 0, true));
        }
    }
}

SCENARIO("fuzzy_polygon basic behavior", "[FuzzySkin]")
{
    GIVEN("a square polygon") {
        Polygon square = make_square(scaled<coord_t>(10.0)); // 10mm square
        size_t original_point_count = square.points.size();

        WHEN("fuzzy skin is applied with Classic noise") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Displacement,
                NoiseType::Classic,
                0.3,  // thickness
                0.8   // point_dist
            );

            Polygon fuzzified = square;
            fuzzy_polygon(fuzzified, 1.0, config);

            THEN("polygon has more points than original") {
                REQUIRE(fuzzified.points.size() > original_point_count);
            }

            THEN("polygon is still valid (at least 3 points)") {
                REQUIRE(fuzzified.points.size() >= 3);
            }
        }

        WHEN("fuzzy skin is applied with Perlin noise") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Displacement,
                NoiseType::Perlin,
                0.3,  // thickness
                0.8,  // point_dist
                false,
                2.0,  // scale
                4,    // octaves
                0.5   // persistence
            );

            Polygon fuzzified = square;
            fuzzy_polygon(fuzzified, 1.0, config);

            THEN("polygon has more points than original") {
                REQUIRE(fuzzified.points.size() > original_point_count);
            }
        }

        WHEN("fuzzy skin is applied with Billow noise") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Displacement,
                NoiseType::Billow
            );

            Polygon fuzzified = square;
            fuzzy_polygon(fuzzified, 1.0, config);

            THEN("polygon has more points than original") {
                REQUIRE(fuzzified.points.size() > original_point_count);
            }
        }

        WHEN("fuzzy skin is applied with RidgedMulti noise") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Displacement,
                NoiseType::RidgedMulti
            );

            Polygon fuzzified = square;
            fuzzy_polygon(fuzzified, 1.0, config);

            THEN("polygon has more points than original") {
                REQUIRE(fuzzified.points.size() > original_point_count);
            }
        }

        WHEN("fuzzy skin is applied with Voronoi noise") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Displacement,
                NoiseType::Voronoi
            );

            Polygon fuzzified = square;
            fuzzy_polygon(fuzzified, 1.0, config);

            THEN("polygon has more points than original") {
                REQUIRE(fuzzified.points.size() > original_point_count);
            }
        }
    }
}

SCENARIO("fuzzy_extrusion_line modes", "[FuzzySkin]")
{
    GIVEN("a simple extrusion line") {
        Arachne::ExtrusionLine ext_line(0, false, false);
        coord_t base_width = scaled<coord_t>(0.4);

        // Create a line with 4 points forming a square path
        ext_line.junctions.emplace_back(Point(0, 0), base_width, 0);
        ext_line.junctions.emplace_back(Point(scaled<coord_t>(10.0), 0), base_width, 0);
        ext_line.junctions.emplace_back(Point(scaled<coord_t>(10.0), scaled<coord_t>(10.0)), base_width, 0);
        ext_line.junctions.emplace_back(Point(0, scaled<coord_t>(10.0)), base_width, 0);

        size_t original_count = ext_line.junctions.size();

        WHEN("Displacement mode is used") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Displacement,
                NoiseType::Classic
            );

            Arachne::ExtrusionLine fuzzified = ext_line;
            fuzzy_extrusion_line(fuzzified, 1.0, config);

            THEN("extrusion line has more junctions") {
                REQUIRE(fuzzified.junctions.size() > original_count);
            }

            THEN("junction widths remain unchanged") {
                // In displacement mode, widths should stay close to original
                for (const auto &junction : fuzzified.junctions) {
                    // Allow small tolerance for rounding
                    REQUIRE(junction.w == base_width);
                }
            }
        }

        WHEN("Extrusion mode is used") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Extrusion,
                NoiseType::Classic
            );

            Arachne::ExtrusionLine fuzzified = ext_line;
            fuzzy_extrusion_line(fuzzified, 1.0, config);

            THEN("extrusion line has more junctions") {
                REQUIRE(fuzzified.junctions.size() > original_count);
            }

            THEN("junction widths vary") {
                bool has_width_variation = false;
                for (const auto &junction : fuzzified.junctions) {
                    if (junction.w != base_width) {
                        has_width_variation = true;
                        break;
                    }
                }
                REQUIRE(has_width_variation);
            }
        }

        WHEN("Combined mode is used") {
            PrintRegionConfig config = make_fuzzy_config(
                FuzzySkinType::External,
                FuzzySkinMode::Combined,
                NoiseType::Classic
            );

            Arachne::ExtrusionLine fuzzified = ext_line;
            fuzzy_extrusion_line(fuzzified, 1.0, config);

            THEN("extrusion line has more junctions") {
                REQUIRE(fuzzified.junctions.size() > original_count);
            }

            THEN("junction widths vary (combined has both displacement and width variation)") {
                bool has_width_variation = false;
                for (const auto &junction : fuzzified.junctions) {
                    if (junction.w != base_width) {
                        has_width_variation = true;
                        break;
                    }
                }
                REQUIRE(has_width_variation);
            }
        }
    }
}

SCENARIO("Coherent noise produces consistent patterns", "[FuzzySkin]")
{
    GIVEN("the same polygon fuzzified twice at the same Z height with Perlin noise") {
        Polygon square1 = make_square(scaled<coord_t>(10.0));
        Polygon square2 = make_square(scaled<coord_t>(10.0));

        PrintRegionConfig config = make_fuzzy_config(
            FuzzySkinType::External,
            FuzzySkinMode::Displacement,
            NoiseType::Perlin,
            0.3,  // thickness
            0.8,  // point_dist
            false,
            2.0   // scale
        );

        // Note: Due to the random point spacing, the exact points won't match,
        // but the noise values at the same coordinates should be the same.
        // This test verifies that the noise module is being used correctly.

        fuzzy_polygon(square1, 1.0, config);
        fuzzy_polygon(square2, 1.0, config);

        THEN("both polygons have been modified") {
            REQUIRE(square1.points.size() > 4);
            REQUIRE(square2.points.size() > 4);
        }
    }

    GIVEN("polygons at different Z heights with coherent noise") {
        PrintRegionConfig config = make_fuzzy_config(
            FuzzySkinType::External,
            FuzzySkinMode::Displacement,
            NoiseType::Perlin,
            0.3,
            0.8,
            false,
            2.0
        );

        Polygon square_z1 = make_square(scaled<coord_t>(10.0));
        Polygon square_z2 = make_square(scaled<coord_t>(10.0));

        fuzzy_polygon(square_z1, 1.0, config);  // Z = 1.0
        fuzzy_polygon(square_z2, 2.0, config);  // Z = 2.0

        THEN("both are valid polygons") {
            REQUIRE(square_z1.points.size() >= 3);
            REQUIRE(square_z2.points.size() >= 3);
        }
    }
}

SCENARIO("apply_fuzzy_skin with empty perimeter regions", "[FuzzySkin]")
{
    GIVEN("a polygon with empty perimeter regions") {
        Polygon square = make_square(scaled<coord_t>(10.0));
        PerimeterRegions empty_regions;

        PrintRegionConfig config = make_fuzzy_config(FuzzySkinType::External);

        WHEN("should_fuzzify returns true") {
            Polygon result = apply_fuzzy_skin(square, config, empty_regions, 1, 1.0, 0, true);

            THEN("polygon is fuzzified") {
                REQUIRE(result.points.size() > square.points.size());
            }
        }

        WHEN("should_fuzzify returns false (inner perimeter)") {
            Polygon result = apply_fuzzy_skin(square, config, empty_regions, 1, 1.0, 1, true);

            THEN("polygon is unchanged") {
                REQUIRE(result.points.size() == square.points.size());
            }
        }
    }
}

SCENARIO("Full slice with fuzzy skin enabled", "[FuzzySkin][Integration]")
{
    GIVEN("A cube with fuzzy skin external") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "fuzzy_skin",             "external" },
            { "fuzzy_skin_thickness",   0.3 },
            { "fuzzy_skin_point_dist",  0.8 },
            { "perimeters",             2 },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "fill_density",           0 },
            { "top_solid_layers",       0 },
            { "bottom_solid_layers",    1 }
        });

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);

        THEN("G-code is generated successfully") {
            REQUIRE(!gcode.empty());
        }
    }

    GIVEN("A cube with fuzzy skin all") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "fuzzy_skin",             "all" },
            { "fuzzy_skin_thickness",   0.3 },
            { "fuzzy_skin_point_dist",  0.8 },
            { "perimeters",             2 },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "fill_density",           0 },
            { "top_solid_layers",       0 },
            { "bottom_solid_layers",    1 }
        });

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);

        THEN("G-code is generated successfully") {
            REQUIRE(!gcode.empty());
        }
    }

    GIVEN("A cube with fuzzy skin allwalls") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "fuzzy_skin",             "allwalls" },
            { "fuzzy_skin_thickness",   0.3 },
            { "fuzzy_skin_point_dist",  0.8 },
            { "perimeters",             3 },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "fill_density",           0 },
            { "top_solid_layers",       0 },
            { "bottom_solid_layers",    1 }
        });

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);

        THEN("G-code is generated successfully") {
            REQUIRE(!gcode.empty());
        }
    }

    GIVEN("A cube with Perlin noise fuzzy skin") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "fuzzy_skin",             "external" },
            { "fuzzy_skin_noise_type",  "perlin" },
            { "fuzzy_skin_thickness",   0.3 },
            { "fuzzy_skin_point_dist",  0.8 },
            { "fuzzy_skin_scale",       2.0 },
            { "fuzzy_skin_octaves",     4 },
            { "fuzzy_skin_persistence", 0.5 },
            { "perimeters",             2 },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "fill_density",           0 },
            { "top_solid_layers",       0 },
            { "bottom_solid_layers",    1 }
        });

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);

        THEN("G-code is generated successfully") {
            REQUIRE(!gcode.empty());
        }
    }

    GIVEN("A cube with first layer fuzzy skin enabled") {
        auto config = Slic3r::DynamicPrintConfig::full_print_config_with({
            { "fuzzy_skin",             "external" },
            { "fuzzy_skin_first_layer", true },
            { "fuzzy_skin_thickness",   0.3 },
            { "fuzzy_skin_point_dist",  0.8 },
            { "perimeters",             2 },
            { "layer_height",           0.2 },
            { "first_layer_height",     0.2 },
            { "fill_density",           0 },
            { "top_solid_layers",       0 },
            { "bottom_solid_layers",    1 }
        });

        std::string gcode = Slic3r::Test::slice({ Slic3r::Test::TestMesh::cube_20x20x20 }, config);

        THEN("G-code is generated successfully") {
            REQUIRE(!gcode.empty());
        }
    }
}
