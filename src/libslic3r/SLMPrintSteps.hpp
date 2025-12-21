///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SLMPrintSteps_hpp_
#define slic3r_SLMPrintSteps_hpp_

#include "SLMPrint.hpp"

namespace Slic3r {

// Forward declarations
class SLMPrint;
class SLMPrintObject;

// Processing steps implementation for SLM printing
// This will be implemented in SLMPrintSteps.cpp
class SLMPrint::Steps
{
public:
    Steps(SLMPrint *print) : m_print(print) {}

    // Process all objects through all steps
    void process_objects(const std::vector<SLMPrintObjectStep>& steps, double& obj_status, double& print_status);
    
    // Process print-level steps
    void process_print(const std::vector<SLMPrintStep>& steps, double& print_status);

    // Slice a single SLM print object
    void slice_model(SLMPrintObject &po);

    // Generate hatch patterns from slices
    void generate_hatches(SLMPrintObject &po);

    // Generate scan paths from hatch patterns
    void generate_scan_paths(SLMPrintObject &po);

private:
    SLMPrint *m_print;
    
    static const double min_objstatus;
    static const double min_printstatus;

    // Helper methods for hatch generation
    void generate_grid_hatch(const ExPolygons &slice, double spacing, double angle, Polylines &hatch_out);
    void generate_stripe_hatch(const ExPolygons &slice, double spacing, double angle, Polylines &hatch_out);
    void generate_concentric_hatch(const ExPolygons &slice, double spacing, Polylines &hatch_out);
    void extract_contours(const ExPolygons &slice, Polylines &contours_out);
    
    // Scan path generation helpers
    void optimize_scan_order(SLMPrintObject &po);
    void optimize_vector_order(ScanVectors &vectors);
};

} // namespace Slic3r

#endif /* slic3r_SLMPrintSteps_hpp_ */

