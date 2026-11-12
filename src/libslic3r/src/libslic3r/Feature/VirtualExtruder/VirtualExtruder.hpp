#pragma once

#include "Slic3r/Domain/VirtualExtruder.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"

#include <optional>
#include <vector>

namespace Slic3r {
class PrintObject;
} // namespace Slic3r

namespace Slic3r::Feature::VirtualExtruder {

/**
 * @brief Resolve which physical extruder to use on each layer.
 *
 * For blends, builds a repeating cycle from component ratios.
 * For gradients with explicit z_min/z_max, delegates to
 * resolve_gradient_with_ranges(). For auto-gradients (missing
 * z_min or z_max), returns all zeros - the caller must detect
 * ranges from segmentation content and call
 * resolve_gradient_with_ranges() directly.
 *
 * @param print_z_per_layer Z height of each layer.
 * @return 1-based physical extruder ID per layer, or 0 if unresolved.
 */
std::vector<unsigned int> resolve_all_layers(
    const Domain::VirtualExtruder& virtual_extruder,
    const std::vector<double>& print_z_per_layer
);

/**
 * @brief Resolve gradient to physical extruder IDs within given Z ranges.
 *
 * Divides each Z range into bands (clamped to GRADIENT_MIN_LAYERS_PER_BAND
 * ..GRADIENT_MAX_BANDS). Each band interpolates gradient stop weights at
 * its midpoint, quantizes them, and builds a deficit-based cycle of
 * physical extruder IDs. Layers are assigned from the cycle of their band.
 *
 * @param print_z_per_layer Z height of each layer.
 * @param z_ranges          Pairs of (z_min, z_max) defining gradient spans.
 * @return 1-based physical extruder ID per layer, or 0 for layers outside ranges.
 */
std::vector<unsigned int> resolve_gradient_with_ranges(
    const Domain::VirtualExtruder& virtual_extruder,
    const std::vector<double>& print_z_per_layer,
    const std::vector<std::pair<double, double>>& z_ranges
);

/**
 * @brief Detect contiguous Z ranges where layers have content.
 *
 * Scans layer_has_content for runs of true values and returns their
 * Z spans as (z_first, z_last) pairs. Used by auto-gradient mode
 * to determine where the gradient should apply.
 *
 * @param print_z_per_layer Z height of each layer.
 * @param layer_has_content Per-layer flag indicating painted content.
 * @return Vector of (z_min, z_max) pairs for contiguous content runs.
 */
std::vector<std::pair<double, double>> detect_gradient_ranges(
    const std::vector<double>& print_z_per_layer,
    const std::vector<bool>& layer_has_content
);

/**
 * @brief Move slices from virtual-extruder PrintRegions to physical ones.
 *
 * For each PrintRegion tagged with a source virtual extruder, resolves
 * which physical extruder to use per layer, then moves
 * ExPolygons from the virtual LayerRegion to the matching physical
 * LayerRegion. Operates on the non-painted remainder after MM segmentation.
 *
 * @param print_object       The print object whose layers are modified in-place.
 * @param num_physical       Number of physical extruders.
 * @param virtual_extruders  Filtered virtual extruder definitions.
 */
void remap_virtual_region_slices_to_physical(
    PrintObject& print_object,
    unsigned int num_physical,
    const Domain::VirtualExtruders& virtual_extruders
);

/**
 * @brief Remap MM-painted segmentation from virtual to physical extruder slots.
 *
 * For each virtual extruder slot in the segmentation array, resolves which
 * physical extruder to use per layer, then moves
 * ExPolygons from virtual slots to the corresponding physical slots.
 * After return, all virtual slots are empty.
 *
 * @param[in,out] segmentation        Per-layer array of ExPolygons per extruder slot.
 * @param         print_z_per_layer   Z height of each layer.
 * @param         num_physical_extruders Number of physical extruders.
 * @param         virtual_extruders   Filtered virtual extruder definitions.
 */
void remap_virtual_extruders_to_physical(
    std::vector<std::vector<Domain::ExPolygons>>& segmentation,
    const std::vector<double>& print_z_per_layer,
    unsigned int num_physical_extruders,
    const Domain::VirtualExtruders& virtual_extruders
);

} // namespace Slic3r::Feature::VirtualExtruder
