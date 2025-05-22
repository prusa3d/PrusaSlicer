///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_MultiMaterialAutoColorization_hpp_
#define slic3r_MultiMaterialAutoColorization_hpp_

#include <vector>
#include <functional>
#include <memory>
#include <optional>

#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

namespace Slic3r {

class ModelObject;
class ModelVolume;

enum class MMUAutoColorizationPattern : int {
    HeightGradient,
    RadialGradient,
    SpiralPattern,
    NoisePattern,
    OptimizedChanges,
    Count
};

struct MMUAutoColorizationParams {
    MMUAutoColorizationPattern pattern_type = MMUAutoColorizationPattern::HeightGradient;
    
    // Which extruders to use (1-based indices, 0 means not used)
    std::vector<int> extruders = {1, 2, 0, 0, 0};
    
    // Percentage-based distribution for each extruder (0-100)
    std::vector<float> distribution = {50.0f, 50.0f, 0.0f, 0.0f, 0.0f};
    
    // Pattern-specific parameters
    
    // For height gradient
    float height_start_percent = 0.0f;  // Start height as percentage of total height
    float height_end_percent = 100.0f;  // End height as percentage of total height
    bool height_reverse = false;        // Reverse the gradient direction
    
    // For radial gradient
    Vec3f radial_center = Vec3f::Zero(); // Center point of the radial gradient
    float radial_radius = 50.0f;        // Radius of the gradient in mm
    bool radial_reverse = false;        // Reverse the gradient direction
    
    // For spiral pattern
    Vec3f spiral_center = Vec3f::Zero(); // Center point of the spiral
    float spiral_pitch = 10.0f;         // Distance between spiral turns in mm
    int spiral_turns = 5;               // Number of complete turns
    bool spiral_reverse = false;        // Reverse the spiral direction
    
    // For noise pattern
    float noise_scale = 10.0f;          // Scale of the noise pattern
    float noise_threshold = 0.5f;       // Threshold for noise pattern
    int noise_seed = 1234;              // Seed for the noise generator
    
    // For optimized changes
    int min_area_per_color = 100;       // Minimum area (in mm²) per color to reduce color changes
};

// Apply automatic colorization to a model object based on the specified parameters
void apply_auto_colorization(ModelObject& model_object, const MMUAutoColorizationParams& params);

// Generate a preview of the auto-colorization without modifying the model
// Returns a vector of triangle selectors with the colorization applied
std::vector<std::unique_ptr<TriangleSelectorGUI>> preview_auto_colorization(
    const ModelObject& model_object, 
    const MMUAutoColorizationParams& params);

// Helper function to validate and normalize the auto-colorization parameters
MMUAutoColorizationParams validate_auto_colorization_params(const MMUAutoColorizationParams& params);

} // namespace Slic3r

#endif // slic3r_MultiMaterialAutoColorization_hpp_
