#ifndef SLM_TEST_UTILS_HPP
#define SLM_TEST_UTILS_HPP

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <test_utils.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/SLMPrint.hpp"
#include "libslic3r/SLMScanPath.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <string>
#include <vector>

using namespace Slic3r;
using Catch::Approx;

// Helper function to create a simple test mesh (cube)
TriangleMesh create_test_cube(double size = 20.0);

// Helper function to create a test model
Model create_test_model(const std::string &name, TriangleMesh &&mesh);

// Helper function to initialize SLM print with default config
void init_slm_print(
    SLMPrint &print,
    Model &model,
    const DynamicPrintConfig &config_in = DynamicPrintConfig::full_print_config()
);

// Helper function to create default SLM config
DynamicPrintConfig create_default_slm_config();

// Helper function to validate scan paths
void validate_scan_paths(const std::vector<LayerScanPaths> &scan_paths);

// Helper function to check if scan vectors are valid
bool is_valid_scan_vector(const ScanVector &vec);

// Helper function to count scan vectors in layer
size_t count_scan_vectors(const LayerScanPaths &layer);

// Helper function to check layer height consistency
void validate_layer_heights(const std::vector<float> &layer_heights, double expected_thickness);

#endif // SLM_TEST_UTILS_HPP

