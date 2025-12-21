///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLMExporter_hpp_
#define slic3r_SLMExporter_hpp_

#include <memory>
#include <vector>
#include <string>

// Include SLMScanPath for ScanVector, ScanVectors, and LayerScanPaths definitions
// since they're used in function signatures
#include "SLMScanPath.hpp"

// Include libSLM Layer.h for HatchGeometry and ContourGeometry type aliases
// These are type aliases (using declarations), not classes, so they can't be forward declared
#include <App/Layer.h>

// Forward declarations
namespace slm {
    class Header;
    class Model;
    class BuildStyle;
}

namespace Slic3r {

// Forward declarations
class SLMPrint;
class SLMPrintObject;

/**
 * @brief Converts PrusaSlicer SLM data to libSLM format
 * 
 * This class handles conversion from PrusaSlicer's internal representation
 * to libSLM's data structures for export.
 */
class SLMExporter
{
public:
    // Convert scan vectors to libSLM HatchGeometry
    static std::shared_ptr<slm::HatchGeometry> convert_hatches_to_geometry(
        const ScanVectors &scan_vectors,
        uint32_t model_id = 1,
        uint32_t build_style_id = 8); // CoreNormalHatch

    // Convert scan vectors to libSLM ContourGeometry
    static std::shared_ptr<slm::ContourGeometry> convert_contours_to_geometry(
        const ScanVectors &scan_vectors,
        uint32_t model_id = 1,
        uint32_t build_style_id = 1); // CoreContour_Volume

    // Create libSLM Layer from LayerScanPaths
    static std::shared_ptr<slm::Layer> create_layer(
        const LayerScanPaths &layer_paths,
        uint64_t layer_id,
        uint32_t model_id = 1,
        bool contour_first = true);

    // Create libSLM Model from SLMPrint
    static std::shared_ptr<slm::Model> create_model(
        const SLMPrint &print,
        uint64_t model_id = 1,
        const SLMPrintObject *print_object = nullptr);

    // Create libSLM BuildStyle from SLMPrintConfig
    static std::shared_ptr<slm::BuildStyle> create_build_style(
        const SLMPrint &print,
        uint64_t build_style_id = 8);

    // Create libSLM Header
    static slm::Header create_header(const SLMPrint &print);

    // Convert scaled coordinate to unscaled (mm)
    static float unscaled(float coord_scaled);
    
    // Convert scaled Point to unscaled (mm)
    static std::pair<float, float> unscaled_point(const Point &pt);
};

} // namespace Slic3r

#endif /* slic3r_SLMExporter_hpp_ */

