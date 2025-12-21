///|/ Copyright (c) Prusa Research 2024
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SLMPrintSteps.hpp"

#include "SLMPrint.hpp"
#include "SLMPrintConfig.hpp"  // For enum definitions
#include "TriangleMeshSlicer.hpp"
#include "Thread.hpp"
#include "BoundingBox.hpp"
#include "Surface.hpp"
#include "Fill/FillRectilinear.hpp"
#include "Fill/FillConcentric.hpp"
#include "ClipperUtils.hpp"
#include "Polyline.hpp"
#include "SLMScanPath.hpp"
#include "Geometry.hpp"
#include "Execution/ExecutionTBB.hpp"

#include <boost/log/trivial.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace Slic3r {

const double SLMPrint::Steps::min_objstatus = 0.;
const double SLMPrint::Steps::min_printstatus = 0.;

void SLMPrint::Steps::process_objects(const std::vector<SLMPrintObjectStep>& steps, double& obj_status, double& print_status)
{
    if (m_print->m_objects.empty())
        return;

    const size_t num_objects = m_print->m_objects.size();
    const size_t num_steps = steps.size();
    const double step_size = 1.0 / (num_objects * num_steps);

    size_t obj_idx = 0;
    for (SLMPrintObject *object : m_print->m_objects) {
        for (SLMPrintObjectStep step : steps) {
            m_print->throw_if_canceled();
            
            if (!object->is_step_done(step)) {
                switch (step) {
                case slmposSlice:
                    slice_model(*object);
                    object->set_done(step);
                    break;
                case slmposGenerateHatches:
                    generate_hatches(*object);
                    generate_scan_paths(*object);
                    object->set_done(step);
                    break;
                default:
                    break;
                }
            }
            
            obj_status += step_size;
            print_status = obj_status;
        }
        ++obj_idx;
    }
}

void SLMPrint::Steps::process_print(const std::vector<SLMPrintStep>& steps, double& print_status)
{
    for (SLMPrintStep step : steps) {
        m_print->throw_if_canceled();
        
        if (!m_print->is_step_done(step)) {
            switch (step) {
            case slmpsSlice:
                // Slice step is handled at object level
                m_print->set_done(step);
                break;
            case slmpsGenerateHatches:
                // TODO: Phase 4 - Implement print-level hatch generation
                // This might merge hatches from multiple objects
                m_print->set_done(step);
                break;
            default:
                break;
            }
        }
        
        print_status += 1.0 / steps.size();
    }
}

void SLMPrint::Steps::slice_model(SLMPrintObject &po)
{
    // Get the model object's mesh
    const ModelObject *model_object = po.model_object();
    if (!model_object || model_object->volumes.empty())
        return;

    // Get layer thickness from print config
    double layer_thickness = m_print->m_print_config.slm_layer_thickness.value;
    if (layer_thickness <= 0.0)
        layer_thickness = 0.05; // Default 50 microns

    // Combine all volumes into a single mesh
    TriangleMesh combined_mesh;
    for (const ModelVolume *volume : model_object->volumes) {
        if (volume->is_model_part()) {
            TriangleMesh volume_mesh = volume->mesh();
            volume_mesh.transform(volume->get_matrix());
            if (combined_mesh.empty())
                combined_mesh = std::move(volume_mesh);
            else
                combined_mesh.merge(volume_mesh);
        }
    }

    if (combined_mesh.empty())
        return;

    // Get bounding box
    BoundingBoxf3 bb = combined_mesh.bounding_box();
    double minZ = bb.min.z();
    double maxZ = bb.max.z();

    // Generate layer height levels
    std::vector<float> zs;
    zs.reserve(size_t((maxZ - minZ) / layer_thickness) + 1);
    
    // Start from minZ + layer_thickness (first layer top)
    for (double z = minZ + layer_thickness; z <= maxZ; z += layer_thickness) {
        zs.push_back(float(z));
    }

    if (zs.empty())
        return;

    // Store layer height levels
    po.layer_height_levels() = zs;

    // Prepare slicing parameters
    MeshSlicingParamsEx params;
    params.mode = MeshSlicingParams::SlicingMode::Regular;
    params.closing_radius = 0.0f; // No closing radius for now
    params.trafo = Transform3d::Identity();

    // Slice the mesh
    auto throw_on_cancel = [this]() { m_print->throw_if_canceled(); };
    po.model_slices() = slice_mesh_ex(combined_mesh.its, zs, params, throw_on_cancel);

    BOOST_LOG_TRIVIAL(info) << "SLM: Sliced object into " << po.model_slices().size() << " layers";
}

void SLMPrint::Steps::generate_hatches(SLMPrintObject &po)
{
    // Get configuration
    const SLMPrintConfig &print_config = m_print->m_print_config;
    SLMHatchPatternType pattern_type = print_config.slm_hatch_pattern.value;
    double hatch_spacing = print_config.slm_hatch_spacing.value;
    double hatch_angle = print_config.slm_hatch_angle.value * M_PI / 180.0; // Convert to radians
    bool contour_first = print_config.slm_contour_first.value;

    // Initialize storage
    po.hatch_patterns().clear();
    po.contours().clear();
    po.hatch_patterns().reserve(po.model_slices().size());
    po.contours().reserve(po.model_slices().size());

    // Process each layer
    for (size_t layer_idx = 0; layer_idx < po.model_slices().size(); ++layer_idx) {
        m_print->throw_if_canceled();

        const ExPolygons &slice = po.model_slices()[layer_idx];
        if (slice.empty())
            continue;

        Polylines layer_hatches;
        Polylines layer_contours;

        // Extract contours from slice
        extract_contours(slice, layer_contours);

        // Support per-layer pattern changes: alternate patterns every N layers
        // This allows for advanced strategies like alternating grid/stripe patterns
        SLMHatchPatternType layer_pattern = pattern_type;
        double layer_hatch_angle = hatch_angle;
        
        // Advanced: Per-layer pattern variation
        // Example: Alternate between grid and stripe every 5 layers
        // This can be enhanced with config options later
        const bool enable_pattern_alternation = false; // Feature flag
        if (enable_pattern_alternation && pattern_type == SLMHatchPatternType::slmhpGrid) {
            const size_t alternation_period = 5;
            if ((layer_idx / alternation_period) % 2 == 1) {
                layer_pattern = SLMHatchPatternType::slmhpStripe;
            }
        }
        
        // Advanced: Per-layer angle variation
        // Rotate angle by a small increment each layer for better material properties
        const bool enable_angle_rotation = false; // Feature flag
        if (enable_angle_rotation) {
            const double angle_increment = 2.0 * M_PI / 180.0; // 2 degrees per layer
            layer_hatch_angle = hatch_angle + (angle_increment * layer_idx);
        }

        // Generate hatch pattern based on type (may vary per layer)
        switch (layer_pattern) {
        case SLMHatchPatternType::slmhpGrid:
            generate_grid_hatch(slice, hatch_spacing, layer_hatch_angle, layer_hatches);
            break;
        case SLMHatchPatternType::slmhpStripe:
            generate_stripe_hatch(slice, hatch_spacing, layer_hatch_angle, layer_hatches);
            break;
        case SLMHatchPatternType::slmhpConcentric:
            generate_concentric_hatch(slice, hatch_spacing, layer_hatches);
            break;
        case SLMHatchPatternType::slmhpCustom:
            // Custom pattern: Supports custom angle variations
            // Can be enhanced to support complex custom patterns
            generate_grid_hatch(slice, hatch_spacing, layer_hatch_angle, layer_hatches);
            break;
        }

        // Apply layer rotation if configured
        double rotation_angle = m_print->m_print_config.slm_rotation_angle.value * M_PI / 180.0;
        if (rotation_angle != 0.0) {
            double layer_rotation = rotation_angle * layer_idx;
            BoundingBox bbox = get_extents(slice);
            Point center = bbox.center();
            for (Polyline &hatch : layer_hatches) {
                for (Point &pt : hatch.points) {
                    Point translated = pt - center;
                    double cos_r = std::cos(layer_rotation);
                    double sin_r = std::sin(layer_rotation);
                    Point rotated(
                        coord_t(translated.x() * cos_r - translated.y() * sin_r),
                        coord_t(translated.x() * sin_r + translated.y() * cos_r)
                    );
                    pt = rotated + center;
                }
            }
        }

        // Store results (order based on contour_first setting)
        if (contour_first) {
            po.contours().push_back(std::move(layer_contours));
            po.hatch_patterns().push_back(std::move(layer_hatches));
        } else {
            po.hatch_patterns().push_back(std::move(layer_hatches));
            po.contours().push_back(std::move(layer_contours));
        }
    }

    BOOST_LOG_TRIVIAL(info) << "SLM: Generated hatch patterns for " << po.hatch_patterns().size() << " layers";
}

void SLMPrint::Steps::extract_contours(const ExPolygons &slice, Polylines &contours_out)
{
    contours_out.clear();
    for (const ExPolygon &expoly : slice) {
        // Add outer contour
        Polyline contour;
        contour.points = expoly.contour.points;
        if (!contour.points.empty() && contour.points.front() != contour.points.back()) {
            contour.points.push_back(contour.points.front()); // Close the loop
        }
        contours_out.push_back(std::move(contour));
    }
}

void SLMPrint::Steps::generate_grid_hatch(const ExPolygons &slice, double spacing, double angle, Polylines &hatch_out)
{
    // Use FillGrid for grid pattern
    FillGrid fill;
    fill.spacing = spacing;
    fill.angle = float(angle);
    fill.bounding_box = get_extents(slice);

    // Generate fill for each ExPolygon in the slice
    FillParams params;
    params.density = 1.0f; // Full density for SLM
    params.dont_adjust = true;
    params.resolution = 0.0125;

    hatch_out.clear();
    for (const ExPolygon &expoly : slice) {
        Surface surface(stInternal, expoly);
        Polylines poly = fill.fill_surface(&surface, params);
        append(hatch_out, poly);
    }
}

void SLMPrint::Steps::generate_stripe_hatch(const ExPolygons &slice, double spacing, double angle, Polylines &hatch_out)
{
    // Use FillRectilinear for single-direction lines
    FillRectilinear fill;
    fill.spacing = spacing;
    fill.angle = float(angle);
    fill.bounding_box = get_extents(slice);

    // Generate fill for each ExPolygon in the slice
    FillParams params;
    params.density = 1.0f; // Full density for SLM
    params.dont_adjust = true;
    params.resolution = 0.0125;

    hatch_out.clear();
    for (const ExPolygon &expoly : slice) {
        Surface surface(stInternal, expoly);
        Polylines poly = fill.fill_surface(&surface, params);
        append(hatch_out, poly);
    }
}

void SLMPrint::Steps::generate_concentric_hatch(const ExPolygons &slice, double spacing, Polylines &hatch_out)
{
    // Use FillConcentric
    FillConcentric fill;
    fill.spacing = spacing;
    fill.angle = 0.0f; // Concentric doesn't use angle
    fill.bounding_box = get_extents(slice);

    // Generate fill for each ExPolygon in the slice
    FillParams params;
    params.density = 1.0f; // Full density for SLM
    params.dont_adjust = true;
    params.resolution = 0.0125;

    hatch_out.clear();
    for (const ExPolygon &expoly : slice) {
        Surface surface(stInternal, expoly);
        Polylines poly = fill.fill_surface(&surface, params);
        append(hatch_out, poly);
    }
}

void SLMPrint::Steps::generate_scan_paths(SLMPrintObject &po)
{
    // Get configuration
    const SLMPrintConfig &print_config = m_print->m_print_config;
    float laser_power = print_config.slm_laser_power.value;
    float laser_speed = print_config.slm_laser_speed.value;
    float exposure_time = print_config.slm_exposure_time.value;
    double scan_vector_spacing = print_config.slm_scan_vector_spacing.value;

    // Initialize scan paths storage
    po.scan_paths().clear();
    po.scan_paths().reserve(po.model_slices().size());

    // Process each layer
    for (size_t layer_idx = 0; layer_idx < po.model_slices().size(); ++layer_idx) {
        m_print->throw_if_canceled();

        float layer_z = (layer_idx < po.layer_height_levels().size()) 
                       ? po.layer_height_levels()[layer_idx] 
                       : 0.0f;

        LayerScanPaths layer_paths(layer_z);

        // Convert contours to scan vectors
        if (layer_idx < po.contours().size()) {
            for (const Polyline &contour : po.contours()[layer_idx]) {
                if (contour.points.size() < 2)
                    continue;

                // Convert polyline segments to scan vectors
                for (size_t i = 0; i < contour.points.size() - 1; ++i) {
                    ScanVector scan_vec(
                        contour.points[i],
                        contour.points[i + 1],
                        laser_power,
                        laser_speed,
                        exposure_time
                    );
                    if (scan_vec.is_valid()) {
                        layer_paths.contours.push_back(scan_vec);
                    }
                }
            }
        }

        // Convert hatch patterns to scan vectors
        if (layer_idx < po.hatch_patterns().size()) {
            for (const Polyline &hatch : po.hatch_patterns()[layer_idx]) {
                if (hatch.points.size() < 2)
                    continue;

                // Convert polyline segments to scan vectors
                // Apply scan vector spacing if needed (subdivide long segments)
                for (size_t i = 0; i < hatch.points.size() - 1; ++i) {
                    Point start = hatch.points[i];
                    Point end = hatch.points[i + 1];
                    
                    // Calculate segment length
                    Point diff = end - start;
                    double segment_length = std::sqrt(double(diff.x() * diff.x() + diff.y() * diff.y()));
                    double scaled_spacing = scale_(scan_vector_spacing);
                    
                    if (segment_length <= scaled_spacing || scan_vector_spacing <= 0.0) {
                        // Single scan vector for short segment or no spacing
                        ScanVector scan_vec(start, end, laser_power, laser_speed, exposure_time);
                        if (scan_vec.is_valid()) {
                            layer_paths.hatches.push_back(scan_vec);
                        }
                    } else {
                        // Subdivide long segment into multiple scan vectors
                        int num_segments = int(std::ceil(segment_length / scaled_spacing));
                        double t_step = 1.0 / num_segments;
                        
                        for (int j = 0; j < num_segments; ++j) {
                            double t1 = j * t_step;
                            double t2 = (j + 1) * t_step;
                            
                            Point seg_start(
                                coord_t(start.x() + (end.x() - start.x()) * t1),
                                coord_t(start.y() + (end.y() - start.y()) * t1)
                            );
                            Point seg_end(
                                coord_t(start.x() + (end.x() - start.x()) * t2),
                                coord_t(start.y() + (end.y() - start.y()) * t2)
                            );
                            
                            ScanVector scan_vec(seg_start, seg_end, laser_power, laser_speed, exposure_time);
                            if (scan_vec.is_valid()) {
                                layer_paths.hatches.push_back(scan_vec);
                            }
                        }
                    }
                }
            }
        }

        // Store scan paths
        po.scan_paths().push_back(std::move(layer_paths));
    }

    // Optimize scan order for efficiency
    optimize_scan_order(po);

    BOOST_LOG_TRIVIAL(info) << "SLM: Generated scan paths for " << po.scan_paths().size() << " layers";
}

void SLMPrint::Steps::optimize_scan_order(SLMPrintObject &po)
{
    // Optimize scan order within each layer to minimize travel distance
    // Uses nearest-neighbor approach: always move to closest unvisited scan vector
    
    const SLMPrintConfig &print_config = m_print->m_print_config;
    SLMScanModeType scan_mode = print_config.slm_scan_mode.value;
    
    for (size_t layer_idx = 0; layer_idx < po.scan_paths().size(); ++layer_idx) {
        m_print->throw_if_canceled();
        
        LayerScanPaths &layer_paths = po.scan_paths()[layer_idx];

        // Advanced scan modes: Different optimization strategies
        switch (scan_mode) {
        case SLMScanModeType::slmsmContourFirst:
            // Optimize contours first, then hatches
            if (!layer_paths.contours.empty()) {
                optimize_vector_order(layer_paths.contours);
            }
            if (!layer_paths.hatches.empty()) {
                optimize_vector_order(layer_paths.hatches);
            }
            break;
            
        case SLMScanModeType::slmsmHatchFirst:
            // Optimize hatches first, then contours
            if (!layer_paths.hatches.empty()) {
                optimize_vector_order(layer_paths.hatches);
            }
            if (!layer_paths.contours.empty()) {
                optimize_vector_order(layer_paths.contours);
            }
            break;
        }
        
        // Advanced: Cross-layer optimization (optional)
        // Connect last vector of previous layer to first vector of current layer
        // This reduces travel moves between layers
        if (layer_idx > 0 && !po.scan_paths()[layer_idx - 1].hatches.empty() && 
            !layer_paths.hatches.empty()) {
            // Find closest connection point between layers
            const ScanVector &last_vec = po.scan_paths()[layer_idx - 1].hatches.back();
            Point last_end = last_vec.end;
            
            // Find closest vector in current layer
            size_t closest_idx = 0;
            double min_dist = std::numeric_limits<double>::max();
            
            for (size_t i = 0; i < layer_paths.hatches.size(); ++i) {
                double dist_to_start = (last_end - layer_paths.hatches[i].start).cast<double>().norm();
                double dist_to_end = (last_end - layer_paths.hatches[i].end).cast<double>().norm();
                double min_dist_to_vec = std::min(dist_to_start, dist_to_end);
                
                if (min_dist_to_vec < min_dist) {
                    min_dist = min_dist_to_vec;
                    closest_idx = i;
                }
            }
            
            // Move closest vector to front if it's not already there
            if (closest_idx > 0) {
                std::swap(layer_paths.hatches[0], layer_paths.hatches[closest_idx]);
                // Reverse if end point is closer
                if ((last_end - layer_paths.hatches[0].end).cast<double>().norm() < 
                    (last_end - layer_paths.hatches[0].start).cast<double>().norm()) {
                    std::swap(layer_paths.hatches[0].start, layer_paths.hatches[0].end);
                }
            }
        }
    }
}

void SLMPrint::Steps::optimize_vector_order(ScanVectors &vectors)
{
    if (vectors.size() <= 1)
        return;

    ScanVectors optimized;
    optimized.reserve(vectors.size());
    
    // Start with first vector
    optimized.push_back(vectors[0]);
    vectors.erase(vectors.begin());

    // Greedy nearest-neighbor: always pick closest remaining vector
    while (!vectors.empty()) {
        Point last_end = optimized.back().end;
        
        // Find closest vector (by start or end point)
        size_t closest_idx = 0;
        double min_dist = std::numeric_limits<double>::max();
        
        for (size_t i = 0; i < vectors.size(); ++i) {
            double dist_to_start = (last_end - vectors[i].start).cast<double>().norm();
            double dist_to_end = (last_end - vectors[i].end).cast<double>().norm();
            double min_dist_to_vec = std::min(dist_to_start, dist_to_end);
            
            if (min_dist_to_vec < min_dist) {
                min_dist = min_dist_to_vec;
                closest_idx = i;
            }
        }

        ScanVector &next_vec = vectors[closest_idx];
        
        // Reverse if end point is closer than start point
        if ((last_end - next_vec.end).cast<double>().norm() < 
            (last_end - next_vec.start).cast<double>().norm()) {
            std::swap(next_vec.start, next_vec.end);
        }
        
        optimized.push_back(next_vec);
        vectors.erase(vectors.begin() + closest_idx);
    }

    vectors = std::move(optimized);
}

} // namespace Slic3r

