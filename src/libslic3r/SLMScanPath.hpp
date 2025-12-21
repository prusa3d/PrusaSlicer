///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLMScanPath_hpp_
#define slic3r_SLMScanPath_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Polyline.hpp"
#include <vector>

namespace Slic3r {

/**
 * @brief Represents a single laser scan vector (line segment)
 * 
 * A ScanVector represents one continuous laser scan from start to end point
 * with associated laser parameters (power, speed, exposure).
 */
struct ScanVector
{
    Point start;           // Start point of scan (scaled coordinates)
    Point end;             // End point of scan (scaled coordinates)
    
    // Laser parameters for this scan vector
    float laser_power;     // Laser power in W
    float laser_speed;     // Scan speed in mm/s
    float exposure_time;   // Exposure time in ms
    
    ScanVector() : laser_power(0.0f), laser_speed(0.0f), exposure_time(0.0f) {}
    
    ScanVector(const Point &start_pt, const Point &end_pt, 
               float power = 0.0f, float speed = 0.0f, float exposure = 0.0f)
        : start(start_pt), end(end_pt), laser_power(power), laser_speed(speed), exposure_time(exposure) {}
    
    // Calculate length of scan vector
    double length() const;
    
    // Check if scan vector is valid (non-zero length)
    bool is_valid() const;
};

using ScanVectors = std::vector<ScanVector>;

/**
 * @brief Represents scan paths for a single layer
 * 
 * Contains all scan vectors (hatches and contours) for one layer,
 * ordered according to scan strategy (contour-first or hatch-first).
 */
struct LayerScanPaths
{
    ScanVectors contours;  // Contour scan vectors (outer perimeters)
    ScanVectors hatches;   // Hatch scan vectors (infill)
    
    // Layer Z height
    float z;
    
    LayerScanPaths() : z(0.0f) {}
    LayerScanPaths(float layer_z) : z(layer_z) {}
    
    // Get total number of scan vectors
    size_t total_vectors() const { return contours.size() + hatches.size(); }
    
    // Check if layer has any scan paths
    bool empty() const { return contours.empty() && hatches.empty(); }
};

using LayerScanPathsList = std::vector<LayerScanPaths>;

} // namespace Slic3r

#endif /* slic3r_SLMScanPath_hpp_ */

