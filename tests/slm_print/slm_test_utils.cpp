#include "slm_test_utils.hpp"

#include "libslic3r/SLMPrint.hpp"
#include "libslic3r/SLMScanPath.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

// Functions are declared in global namespace via using namespace Slic3r in header

TriangleMesh create_test_cube(double size)
{
    TriangleMesh mesh;
    mesh = make_cube(size, size, size);
    return mesh;
}

Model create_test_model(const std::string &name, TriangleMesh &&mesh)
{
    Model model;
    ModelObject *object = model.add_object();
    object->name = name;
    
    ModelVolume *volume = object->add_volume(std::move(mesh));
    volume->name = name + "_volume";
    
    object->add_instance();
    
    return model;
}

void init_slm_print(SLMPrint &print, Model &model, const DynamicPrintConfig &config_in)
{
    // Apply the model and config to the print
    std::vector<std::string> warnings;
    print.apply(model, config_in, &warnings);
}

DynamicPrintConfig create_default_slm_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    
    // Set SLM-specific defaults
    config.set("printer_technology", "SLM");
    config.set("slm_layer_thickness", 0.05);
    config.set("slm_laser_power", 200.0);
    config.set("slm_laser_speed", 1000.0);
    config.set("slm_exposure_time", 100.0);
    config.set("slm_point_distance", 0.05);
    config.set("slm_hatch_pattern", "grid");
    config.set("slm_hatch_spacing", 0.1);
    config.set("slm_hatch_angle", 45.0);
    config.set("slm_contour_first", true);
    config.set("slm_scan_mode", "contour_first");
    config.set("slm_scan_vector_spacing", 0.1);
    config.set("slm_rotation_angle", 67.0);
    config.set("slm_export_format", "slm");
    
    return config;
}

void validate_scan_paths(const std::vector<LayerScanPaths> &scan_paths)
{
    REQUIRE(!scan_paths.empty());
    
    for (const auto &layer : scan_paths) {
        // Each layer should have valid Z height
        REQUIRE(layer.z >= 0.0);
        
        // Scan vectors should be valid
        for (const auto &vec : layer.contours) {
            REQUIRE(is_valid_scan_vector(vec));
        }
        for (const auto &vec : layer.hatches) {
            REQUIRE(is_valid_scan_vector(vec));
        }
    }
}

bool is_valid_scan_vector(const ScanVector &vec)
{
    // Check that start and end points are different
    double dist = (vec.end - vec.start).cast<double>().norm();
    return dist > 0.0;
}

size_t count_scan_vectors(const LayerScanPaths &layer)
{
    return layer.total_vectors();
}

void validate_layer_heights(const std::vector<float> &layer_heights, double expected_thickness)
{
    if (layer_heights.size() < 2) return;
    
    // Check that layer heights are approximately consistent
    for (size_t i = 1; i < layer_heights.size(); ++i) {
        double actual_thickness = layer_heights[i] - layer_heights[i-1];
        REQUIRE(actual_thickness == Approx(expected_thickness).margin(0.001));
    }
}

