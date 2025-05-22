///|/ Copyright (c) Prusa Research 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MultiMaterialAutoColorization.hpp"

#include <algorithm>
#include <random>
#include <cmath>

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {

// Perlin noise implementation for the noise pattern
namespace {
    class PerlinNoise {
    private:
        std::vector<int> p;

    public:
        PerlinNoise(int seed = 0) {
            // Initialize the permutation vector with the reference values
            p = {
                151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225,
                140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148,
                247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
                57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175,
                74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122,
                60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54,
                65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
                200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64,
                52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212,
                207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213,
                119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
                129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104,
                218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241,
                81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157,
                184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93,
                222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
            };

            // Duplicate the permutation vector
            p.insert(p.end(), p.begin(), p.end());

            // If a seed is provided, shuffle the permutation vector
            if (seed > 0) {
                std::mt19937 rng(seed);
                std::shuffle(p.begin(), p.end(), rng);
            }
        }

        double noise(double x, double y, double z) {
            // Find the unit cube that contains the point
            int X = static_cast<int>(std::floor(x)) & 255;
            int Y = static_cast<int>(std::floor(y)) & 255;
            int Z = static_cast<int>(std::floor(z)) & 255;

            // Find relative x, y, z of point in cube
            x -= std::floor(x);
            y -= std::floor(y);
            z -= std::floor(z);

            // Compute fade curves for each of x, y, z
            double u = fade(x);
            double v = fade(y);
            double w = fade(z);

            // Hash coordinates of the 8 cube corners
            int A = p[X] + Y;
            int AA = p[A] + Z;
            int AB = p[A + 1] + Z;
            int B = p[X + 1] + Y;
            int BA = p[B] + Z;
            int BB = p[B + 1] + Z;

            // Add blended results from 8 corners of cube
            double res = lerp(w, lerp(v, lerp(u, grad(p[AA], x, y, z), 
                                                grad(p[BA], x-1, y, z)),
                                        lerp(u, grad(p[AB], x, y-1, z), 
                                                grad(p[BB], x-1, y-1, z))),
                                lerp(v, lerp(u, grad(p[AA+1], x, y, z-1), 
                                                grad(p[BA+1], x-1, y, z-1)),
                                        lerp(u, grad(p[AB+1], x, y-1, z-1),
                                                grad(p[BB+1], x-1, y-1, z-1))));
            return (res + 1.0) / 2.0;
        }

    private:
        double fade(double t) { 
            return t * t * t * (t * (t * 6 - 15) + 10);
        }

        double lerp(double t, double a, double b) { 
            return a + t * (b - a); 
        }

        double grad(int hash, double x, double y, double z) {
            int h = hash & 15;
            // Convert lower 4 bits of hash into 12 gradient directions
            double u = h < 8 ? x : y,
                   v = h < 4 ? y : h == 12 || h == 14 ? x : z;
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
        }
    };
}

// Helper function to assign a color (extruder) based on a normalized value and distribution
int assign_color_from_distribution(float normalized_value, const std::vector<int>& extruders, const std::vector<float>& distribution) {
    if (extruders.empty() || distribution.empty())
        return 0;

    // Calculate cumulative distribution
    std::vector<float> cumulative_dist;
    float sum = 0.0f;
    for (float d : distribution) {
        if (d > 0.0f) {
            sum += d;
            cumulative_dist.push_back(sum);
        }
    }

    // Normalize cumulative distribution
    if (sum > 0.0f) {
        for (float& d : cumulative_dist)
            d /= sum;
    } else {
        return extruders[0]; // Default to first extruder if all distributions are 0
    }

    // Find the appropriate color based on the normalized value
    for (size_t i = 0; i < cumulative_dist.size(); ++i) {
        if (normalized_value <= cumulative_dist[i] && extruders[i] > 0)
            return extruders[i];
    }

    // Default to the last valid extruder
    for (int i = static_cast<int>(extruders.size()) - 1; i >= 0; --i) {
        if (extruders[i] > 0)
            return extruders[i];
    }

    return 0; // Fallback
}

// Apply height gradient colorization
void apply_height_gradient(TriangleSelector& selector, const ModelVolume& volume, const MMUAutoColorizationParams& params) {
    const TriangleMesh& mesh = volume.mesh();
    const Transform3d& volume_transform = volume.get_matrix();
    
    // Get the bounding box to determine height range
    BoundingBoxf3 bbox = mesh.bounding_box().transformed(volume_transform);
    float min_z = bbox.min.z();
    float max_z = bbox.max.z();
    float height_range = max_z - min_z;
    
    // Calculate start and end heights based on percentages
    float start_height = min_z + (params.height_start_percent / 100.0f) * height_range;
    float end_height = min_z + (params.height_end_percent / 100.0f) * height_range;
    
    // Process each triangle
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& indices = mesh.its.indices[i];
        
        // Calculate the center of the triangle
        Vec3f center = Vec3f::Zero();
        for (int j = 0; j < 3; ++j) {
            Vec3f vertex = mesh.its.vertices[indices[j]].cast<float>();
            center += vertex;
        }
        center /= 3.0f;
        
        // Transform the center to world coordinates
        Vec3d center_world = volume_transform * center.cast<double>();
        
        // Calculate normalized height position
        float normalized_height = (center_world.z() - start_height) / (end_height - start_height);
        normalized_height = std::clamp(normalized_height, 0.0f, 1.0f);
        
        // Reverse direction if needed
        if (params.height_reverse)
            normalized_height = 1.0f - normalized_height;
        
        // Assign color based on distribution
        int extruder_id = assign_color_from_distribution(normalized_height, params.extruders, params.distribution);
        
        // Set the triangle state
        if (extruder_id > 0)
            selector.set_facet(i, TriangleStateType(extruder_id));
    }
}

// Apply radial gradient colorization
void apply_radial_gradient(TriangleSelector& selector, const ModelVolume& volume, const MMUAutoColorizationParams& params) {
    const TriangleMesh& mesh = volume.mesh();
    const Transform3d& volume_transform = volume.get_matrix();
    
    // Process each triangle
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& indices = mesh.its.indices[i];
        
        // Calculate the center of the triangle
        Vec3f center = Vec3f::Zero();
        for (int j = 0; j < 3; ++j) {
            Vec3f vertex = mesh.its.vertices[indices[j]].cast<float>();
            center += vertex;
        }
        center /= 3.0f;
        
        // Transform the center to world coordinates
        Vec3d center_world = volume_transform * center.cast<double>();
        
        // Calculate distance from radial center (in XY plane)
        Vec3f radial_center_world = params.radial_center;
        float distance = std::sqrt(std::pow(center_world.x() - radial_center_world.x(), 2) + 
                                  std::pow(center_world.y() - radial_center_world.y(), 2));
        
        // Normalize distance by radius
        float normalized_distance = distance / params.radial_radius;
        normalized_distance = std::clamp(normalized_distance, 0.0f, 1.0f);
        
        // Reverse direction if needed
        if (params.radial_reverse)
            normalized_distance = 1.0f - normalized_distance;
        
        // Assign color based on distribution
        int extruder_id = assign_color_from_distribution(normalized_distance, params.extruders, params.distribution);
        
        // Set the triangle state
        if (extruder_id > 0)
            selector.set_facet(i, TriangleStateType(extruder_id));
    }
}

// Apply spiral pattern colorization
void apply_spiral_pattern(TriangleSelector& selector, const ModelVolume& volume, const MMUAutoColorizationParams& params) {
    const TriangleMesh& mesh = volume.mesh();
    const Transform3d& volume_transform = volume.get_matrix();
    
    // Process each triangle
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& indices = mesh.its.indices[i];
        
        // Calculate the center of the triangle
        Vec3f center = Vec3f::Zero();
        for (int j = 0; j < 3; ++j) {
            Vec3f vertex = mesh.its.vertices[indices[j]].cast<float>();
            center += vertex;
        }
        center /= 3.0f;
        
        // Transform the center to world coordinates
        Vec3d center_world = volume_transform * center.cast<double>();
        
        // Calculate polar coordinates
        Vec3f spiral_center_world = params.spiral_center;
        float dx = center_world.x() - spiral_center_world.x();
        float dy = center_world.y() - spiral_center_world.y();
        float angle = std::atan2(dy, dx);
        if (angle < 0) angle += 2 * M_PI;
        
        // Calculate distance from spiral center
        float distance = std::sqrt(dx*dx + dy*dy);
        
        // Calculate spiral value (combination of angle and distance)
        float spiral_value = (angle / (2 * M_PI) + distance / params.spiral_pitch) / params.spiral_turns;
        spiral_value = std::fmod(spiral_value, 1.0f);
        
        // Reverse direction if needed
        if (params.spiral_reverse)
            spiral_value = 1.0f - spiral_value;
        
        // Assign color based on distribution
        int extruder_id = assign_color_from_distribution(spiral_value, params.extruders, params.distribution);
        
        // Set the triangle state
        if (extruder_id > 0)
            selector.set_facet(i, TriangleStateType(extruder_id));
    }
}

// Apply noise pattern colorization
void apply_noise_pattern(TriangleSelector& selector, const ModelVolume& volume, const MMUAutoColorizationParams& params) {
    const TriangleMesh& mesh = volume.mesh();
    const Transform3d& volume_transform = volume.get_matrix();
    
    // Initialize Perlin noise generator
    PerlinNoise noise(params.noise_seed);
    
    // Process each triangle
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& indices = mesh.its.indices[i];
        
        // Calculate the center of the triangle
        Vec3f center = Vec3f::Zero();
        for (int j = 0; j < 3; ++j) {
            Vec3f vertex = mesh.its.vertices[indices[j]].cast<float>();
            center += vertex;
        }
        center /= 3.0f;
        
        // Transform the center to world coordinates
        Vec3d center_world = volume_transform * center.cast<double>();
        
        // Calculate noise value
        float scale = params.noise_scale / 100.0f; // Convert to a reasonable scale
        float noise_value = noise.noise(
            center_world.x() * scale,
            center_world.y() * scale,
            center_world.z() * scale
        );
        
        // Assign color based on noise value and distribution
        int extruder_id = assign_color_from_distribution(noise_value, params.extruders, params.distribution);
        
        // Set the triangle state
        if (extruder_id > 0)
            selector.set_facet(i, TriangleStateType(extruder_id));
    }
}

// Apply optimized color changes pattern
void apply_optimized_changes(TriangleSelector& selector, const ModelVolume& volume, const MMUAutoColorizationParams& params) {
    // This is a simplified implementation that divides the model into horizontal bands
    // A more sophisticated implementation would use clustering algorithms to minimize color changes
    
    const TriangleMesh& mesh = volume.mesh();
    const Transform3d& volume_transform = volume.get_matrix();
    
    // Get the bounding box to determine height range
    BoundingBoxf3 bbox = mesh.bounding_box().transformed(volume_transform);
    float min_z = bbox.min.z();
    float max_z = bbox.max.z();
    float height_range = max_z - min_z;
    
    // Count active extruders
    int active_extruders = 0;
    for (int e : params.extruders) {
        if (e > 0) active_extruders++;
    }
    
    if (active_extruders == 0) return;
    
    // Calculate band height based on distribution
    float band_height = height_range / active_extruders;
    
    // Process each triangle
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& indices = mesh.its.indices[i];
        
        // Calculate the center of the triangle
        Vec3f center = Vec3f::Zero();
        for (int j = 0; j < 3; ++j) {
            Vec3f vertex = mesh.its.vertices[indices[j]].cast<float>();
            center += vertex;
        }
        center /= 3.0f;
        
        // Transform the center to world coordinates
        Vec3d center_world = volume_transform * center.cast<double>();
        
        // Calculate which band this triangle belongs to
        int band = static_cast<int>((center_world.z() - min_z) / band_height);
        band = std::clamp(band, 0, active_extruders - 1);
        
        // Find the corresponding extruder
        int extruder_idx = 0;
        for (size_t e = 0; e < params.extruders.size(); ++e) {
            if (params.extruders[e] > 0) {
                if (extruder_idx == band) {
                    // Set the triangle state
                    selector.set_facet(i, TriangleStateType(params.extruders[e]));
                    break;
                }
                extruder_idx++;
            }
        }
    }
}

// Validate and normalize the auto-colorization parameters
MMUAutoColorizationParams validate_auto_colorization_params(const MMUAutoColorizationParams& params) {
    MMUAutoColorizationParams validated = params;
    
    // Ensure we have at least one active extruder
    bool has_active_extruder = false;
    for (int e : validated.extruders) {
        if (e > 0) {
            has_active_extruder = true;
            break;
        }
    }
    
    if (!has_active_extruder && !validated.extruders.empty()) {
        validated.extruders[0] = 1; // Set first extruder as active if none are
    }
    
    // Ensure distribution values are valid
    float total_distribution = 0.0f;
    for (float& d : validated.distribution) {
        d = std::max(0.0f, d); // Ensure non-negative
        total_distribution += d;
    }
    
    // Normalize distribution if needed
    if (total_distribution > 0.0f) {
        for (float& d : validated.distribution) {
            d = (d / total_distribution) * 100.0f;
        }
    } else if (!validated.distribution.empty()) {
        // If all distributions are 0, set equal distribution for active extruders
        int active_count = 0;
        for (int e : validated.extruders) {
            if (e > 0) active_count++;
        }
        
        if (active_count > 0) {
            float equal_value = 100.0f / active_count;
            for (size_t i = 0; i < validated.extruders.size() && i < validated.distribution.size(); ++i) {
                validated.distribution[i] = (validated.extruders[i] > 0) ? equal_value : 0.0f;
            }
        }
    }
    
    // Ensure height gradient parameters are valid
    validated.height_start_percent = std::clamp(validated.height_start_percent, 0.0f, 100.0f);
    validated.height_end_percent = std::clamp(validated.height_end_percent, 0.0f, 100.0f);
    
    // Ensure radial gradient parameters are valid
    validated.radial_radius = std::max(0.1f, validated.radial_radius);
    
    // Ensure spiral pattern parameters are valid
    validated.spiral_pitch = std::max(0.1f, validated.spiral_pitch);
    validated.spiral_turns = std::max(1, validated.spiral_turns);
    
    // Ensure noise pattern parameters are valid
    validated.noise_scale = std::max(0.1f, validated.noise_scale);
    validated.noise_threshold = std::clamp(validated.noise_threshold, 0.0f, 1.0f);
    
    return validated;
}

// Apply automatic colorization to a model object
void apply_auto_colorization(ModelObject& model_object, const MMUAutoColorizationParams& params) {
    // Validate and normalize parameters
    MMUAutoColorizationParams validated_params = validate_auto_colorization_params(params);
    
    // Process each volume in the model object
    for (ModelVolume* volume : model_object.volumes) {
        if (!volume->is_model_part())
            continue;
            
        // Create a triangle selector for this volume
        TriangleSelector selector(volume->mesh());
        
        // Apply the selected pattern
        switch (validated_params.pattern_type) {
            case MMUAutoColorizationPattern::HeightGradient:
                apply_height_gradient(selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::RadialGradient:
                apply_radial_gradient(selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::SpiralPattern:
                apply_spiral_pattern(selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::NoisePattern:
                apply_noise_pattern(selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::OptimizedChanges:
                apply_optimized_changes(selector, *volume, validated_params);
                break;
            default:
                // Default to height gradient if unknown pattern type
                apply_height_gradient(selector, *volume, validated_params);
                break;
        }
        
        // Apply the colorization to the volume
        volume->mm_segmentation_facets.set(selector);
    }
}

// Generate a preview of the auto-colorization without modifying the model
std::vector<std::unique_ptr<TriangleSelectorGUI>> preview_auto_colorization(
    const ModelObject& model_object, 
    const MMUAutoColorizationParams& params) 
{
    // Validate and normalize parameters
    MMUAutoColorizationParams validated_params = validate_auto_colorization_params(params);
    
    // Create a vector to store the triangle selectors
    std::vector<std::unique_ptr<TriangleSelectorGUI>> selectors;
    
    // Process each volume in the model object
    for (const ModelVolume* volume : model_object.volumes) {
        if (!volume->is_model_part())
            continue;
            
        // Create a triangle selector for this volume
        auto selector = std::make_unique<TriangleSelectorGUI>(volume->mesh());
        
        // Apply the selected pattern
        switch (validated_params.pattern_type) {
            case MMUAutoColorizationPattern::HeightGradient:
                apply_height_gradient(*selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::RadialGradient:
                apply_radial_gradient(*selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::SpiralPattern:
                apply_spiral_pattern(*selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::NoisePattern:
                apply_noise_pattern(*selector, *volume, validated_params);
                break;
            case MMUAutoColorizationPattern::OptimizedChanges:
                apply_optimized_changes(*selector, *volume, validated_params);
                break;
            default:
                // Default to height gradient if unknown pattern type
                apply_height_gradient(*selector, *volume, validated_params);
                break;
        }
        
        // Add the selector to the vector
        selectors.push_back(std::move(selector));
    }
    
    return selectors;
}

} // namespace Slic3r
