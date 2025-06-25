#include <catch2/catch_test_macros.hpp>
#include "libslic3r/libslic3r.h"

#include "libslic3r/MultiMaterialAutoColorization.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSelector.hpp"

using namespace Slic3r;

// Helper function to create a simple cube model for testing
ModelObject create_test_cube(double size = 20.0) {
    ModelObject obj;
    ModelVolume* volume = obj.add_volume(TriangleMesh::make_cube(size, size, size));
    volume->set_type(ModelVolumeType::MODEL_PART);
    return obj;
}

SCENARIO("MMU Auto-Colorization Parameter Validation", "[MMUAutoColorization]") {
    GIVEN("Default auto-colorization parameters") {
        MMUAutoColorizationParams params;
        
        WHEN("Validating parameters") {
            MMUAutoColorizationParams validated = validate_auto_colorization_params(params);
            
            THEN("Parameters are properly validated") {
                // Check that at least one extruder is active
                bool has_active_extruder = false;
                for (int e : validated.extruders) {
                    if (e > 0) {
                        has_active_extruder = true;
                        break;
                    }
                }
                REQUIRE(has_active_extruder);
                
                // Check that distribution values are normalized
                float total_distribution = 0.0f;
                for (float d : validated.distribution) {
                    total_distribution += d;
                }
                REQUIRE(total_distribution == Approx(100.0f).epsilon(0.01f));
            }
        }
    }
    
    GIVEN("Invalid auto-colorization parameters") {
        MMUAutoColorizationParams params;
        // Set all extruders to inactive
        for (size_t i = 0; i < params.extruders.size(); ++i) {
            params.extruders[i] = 0;
        }
        
        WHEN("Validating parameters") {
            MMUAutoColorizationParams validated = validate_auto_colorization_params(params);
            
            THEN("At least one extruder is activated") {
                bool has_active_extruder = false;
                for (int e : validated.extruders) {
                    if (e > 0) {
                        has_active_extruder = true;
                        break;
                    }
                }
                REQUIRE(has_active_extruder);
            }
        }
    }
    
    GIVEN("Parameters with invalid height range") {
        MMUAutoColorizationParams params;
        params.height_start_percent = -10.0f;
        params.height_end_percent = 110.0f;
        
        WHEN("Validating parameters") {
            MMUAutoColorizationParams validated = validate_auto_colorization_params(params);
            
            THEN("Height range is clamped to valid values") {
                REQUIRE(validated.height_start_percent == 0.0f);
                REQUIRE(validated.height_end_percent == 100.0f);
            }
        }
    }
    
    GIVEN("Parameters with invalid radial and spiral values") {
        MMUAutoColorizationParams params;
        params.radial_radius = -5.0f;
        params.spiral_pitch = 0.0f;
        params.spiral_turns = 0;
        
        WHEN("Validating parameters") {
            MMUAutoColorizationParams validated = validate_auto_colorization_params(params);
            
            THEN("Values are adjusted to valid minimums") {
                REQUIRE(validated.radial_radius > 0.0f);
                REQUIRE(validated.spiral_pitch > 0.0f);
                REQUIRE(validated.spiral_turns >= 1);
            }
        }
    }
}

SCENARIO("MMU Auto-Colorization Color Assignment", "[MMUAutoColorization]") {
    GIVEN("Distribution values and extruders") {
        std::vector<int> extruders = {1, 2, 3, 0, 0};
        std::vector<float> distribution = {30.0f, 30.0f, 40.0f, 0.0f, 0.0f};
        
        WHEN("Assigning colors based on normalized values") {
            int color1 = assign_color_from_distribution(0.0f, extruders, distribution);
            int color2 = assign_color_from_distribution(0.29f, extruders, distribution);
            int color3 = assign_color_from_distribution(0.3f, extruders, distribution);
            int color4 = assign_color_from_distribution(0.59f, extruders, distribution);
            int color5 = assign_color_from_distribution(0.6f, extruders, distribution);
            int color6 = assign_color_from_distribution(1.0f, extruders, distribution);
            
            THEN("Colors are assigned according to distribution") {
                REQUIRE(color1 == 1);
                REQUIRE(color2 == 1);
                REQUIRE(color3 == 2);
                REQUIRE(color4 == 2);
                REQUIRE(color5 == 3);
                REQUIRE(color6 == 3);
            }
        }
    }
    
    GIVEN("Empty extruders or distribution") {
        std::vector<int> empty_extruders;
        std::vector<float> empty_distribution;
        std::vector<int> valid_extruders = {1, 2, 3};
        std::vector<float> valid_distribution = {30.0f, 30.0f, 40.0f};
        
        WHEN("Assigning colors with empty data") {
            int color1 = assign_color_from_distribution(0.5f, empty_extruders, valid_distribution);
            int color2 = assign_color_from_distribution(0.5f, valid_extruders, empty_distribution);
            
            THEN("Default color (0) is returned") {
                REQUIRE(color1 == 0);
                REQUIRE(color2 == 0);
            }
        }
    }
}

SCENARIO("MMU Auto-Colorization Pattern Application", "[MMUAutoColorization]") {
    GIVEN("A simple cube model") {
        ModelObject obj = create_test_cube();
        MMUAutoColorizationParams params;
        params.extruders = {1, 2, 0, 0, 0};
        params.distribution = {50.0f, 50.0f, 0.0f, 0.0f, 0.0f};
        
        WHEN("Applying height gradient pattern") {
            params.pattern_type = MMUAutoColorizationPattern::HeightGradient;
            auto selectors = preview_auto_colorization(obj, params);
            
            THEN("Selectors are created for each volume") {
                REQUIRE(selectors.size() == obj.volumes.size());
                
                // Check that some triangles are colored
                bool has_colored_triangles = false;
                for (const auto& selector : selectors) {
                    if (selector->get_triangle_count() > 0) {
                        has_colored_triangles = true;
                        break;
                    }
                }
                REQUIRE(has_colored_triangles);
            }
        }
        
        WHEN("Applying radial gradient pattern") {
            params.pattern_type = MMUAutoColorizationPattern::RadialGradient;
            auto selectors = preview_auto_colorization(obj, params);
            
            THEN("Selectors are created for each volume") {
                REQUIRE(selectors.size() == obj.volumes.size());
            }
        }
        
        WHEN("Applying spiral pattern") {
            params.pattern_type = MMUAutoColorizationPattern::SpiralPattern;
            auto selectors = preview_auto_colorization(obj, params);
            
            THEN("Selectors are created for each volume") {
                REQUIRE(selectors.size() == obj.volumes.size());
            }
        }
        
        WHEN("Applying noise pattern") {
            params.pattern_type = MMUAutoColorizationPattern::NoisePattern;
            auto selectors = preview_auto_colorization(obj, params);
            
            THEN("Selectors are created for each volume") {
                REQUIRE(selectors.size() == obj.volumes.size());
            }
        }
        
        WHEN("Applying optimized changes pattern") {
            params.pattern_type = MMUAutoColorizationPattern::OptimizedChanges;
            auto selectors = preview_auto_colorization(obj, params);
            
            THEN("Selectors are created for each volume") {
                REQUIRE(selectors.size() == obj.volumes.size());
            }
        }
    }
}

SCENARIO("MMU Auto-Colorization Direct Application", "[MMUAutoColorization]") {
    GIVEN("A simple cube model") {
        ModelObject obj = create_test_cube();
        MMUAutoColorizationParams params;
        params.extruders = {1, 2, 0, 0, 0};
        params.distribution = {50.0f, 50.0f, 0.0f, 0.0f, 0.0f};
        
        WHEN("Applying auto-colorization directly") {
            apply_auto_colorization(obj, params);
            
            THEN("Model volumes have segmentation data") {
                for (const ModelVolume* volume : obj.volumes) {
                    if (volume->is_model_part()) {
                        // Check that segmentation data is not empty
                        REQUIRE(volume->mm_segmentation_facets.get_data().size() > 0);
                    }
                }
            }
        }
    }
}
