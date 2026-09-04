#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

namespace Slic3r::Domain {
class ModelObject;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Algorithms::VirtualExtruder {

/**
 * @brief Check whether a 1-based extruder ID belongs to a virtual extruder.
 */
bool is_virtual_extruder(
    unsigned int extruder_id_1based,
    const Domain::VirtualExtruders& virtual_extruders
);

/**
 * @brief Validate and normalize virtual extruder definitions.
 *
 * For blends: drops negative ratios, enforces 2..MAX_BLEND_COMPONENTS
 * components, normalizes ratios to sum to 1.0.
 * For gradients: sorts stops by position, removes duplicates and
 * out-of-range positions, drops entries with < 2 stops or < 2
 * distinct extruders, validates z_min < z_max.
 *
 * @param raw_virtual_extruders Unvalidated definitions (e.g. from user input).
 * @return Cleaned definitions; invalid entries are dropped with log messages.
 */
Domain::VirtualExtruders normalize_virtual_extruders(
    const Domain::VirtualExtruders& raw_virtual_extruders
);

/**
 * @brief Drop virtual extruders that reference unavailable physical extruders.
 *
 * Checks every component extruder_id against [1, num_physical].
 * Any virtual extruder with an out-of-range reference is dropped entirely.
 *
 * @param num_physical                  Number of physical extruders available.
 * @param normalized_virtual_extruders  Output of normalize_virtual_extruders().
 * @return Filtered list containing only valid entries.
 */
Domain::VirtualExtruders filter_virtual_extruders_for_physical_count(
    unsigned int num_physical,
    const Domain::VirtualExtruders& normalized_virtual_extruders
);

/**
 * @brief Keep only the definitions compatible with a printer having the given slot count.
 *
 * @param virtual_extruders   Definitions to filter (expected to be normalized).
 * @param physical_slot_count Number of physical extruder slots of the printer.
 */
Domain::VirtualExtruders compatible_virtual_extruders(
    const Domain::VirtualExtruders& virtual_extruders,
    unsigned int physical_slot_count
);

/**
 * @brief Replace virtual extruder IDs with their physical component IDs (1-based).
 *
 * Each virtual ID is expanded to the set of physical extruder IDs from
 * its components (blend) or stops (gradient). Non-virtual IDs pass through.
 * Result is sorted and deduplicated.
 */
std::vector<unsigned int> expand_virtual_extruders_1based(
    const std::vector<unsigned int>& extruders_1based,
    const Domain::VirtualExtruders& virtual_extruders
);

/**
 * @brief Replace virtual extruder IDs with their physical component IDs (0-based).
 *
 * Each virtual ID is expanded to the set of physical extruder IDs from
 * its components (blend) or stops (gradient). Non-virtual IDs pass through.
 * Result is sorted and deduplicated.
 */
std::vector<unsigned int> expand_virtual_extruders_0based(
    const std::vector<unsigned int>& extruders_0based,
    const Domain::VirtualExtruders& virtual_extruders
);

/**
 * @brief Compute a display color for this virtual extruder.
 *
 * Returns the user-assigned color if set. Otherwise, blends
 * physical extruder colors by component ratios (blend) or by
 * gradient stop coverage (gradient).
 *
 * @param virtual_extruder                 Virtual extruder to compute the color for.
 * @param physical_extruders_colors_0based 0-based hex color per physical extruder.
 * @return Hex color string (e.g. "#AA5500").
 */
std::string effective_color(
    const Domain::VirtualExtruder& virtual_extruder,
    const std::vector<std::string>& physical_extruders_colors_0based
);

/**
 * @brief Compute a display color for this virtual extruder.
 *
 * RGB overload of the above for the UI: encodes the physical colors to hex,
 * blends, and decodes the result back.
 *
 * @param virtual_extruder                 Virtual extruder to compute the color for.
 * @param physical_extruders_colors_0based 0-based color per physical extruder.
 * @return Blended color, or std::nullopt when the recipe yields no usable color
 *         (the caller substitutes its own fallback, e.g. gray).
 */
std::optional<Domain::ColorRGB> effective_color(
    const Domain::VirtualExtruder& virtual_extruder,
    const std::vector<Domain::ColorRGB>& physical_extruders_colors_0based
);

/**
 * @brief Mix physical extruder colors by component ratios to produce a blended color.
 *
 * Uses prusa_fdm_mixer to blend hex colors weighted by ratios.
 *
 * @param components      Blend components with extruder IDs and ratios.
 * @param physical_colors 0-based hex color per physical extruder.
 * @return Blended hex color string, or empty if no valid components.
 */
std::string blend_virtual_extruder_color(
    const Domain::VirtualExtruderComponents& components,
    const std::vector<std::string>& physical_colors
);

/**
 * @brief Build an evenly-distributed repeating cycle of physical extruder IDs.
 *
 * Quantizes component ratios into integer counts via quantise_component_counts(),
 * then interleaves extruder IDs using a deficit-based algorithm: at each slot,
 * the component whose ideal count most exceeds its emitted count is chosen.
 * This produces maximally uniform distribution (e.g. ratios 2:1 produce
 * [E1, E2, E1] instead of [E1, E1, E2]).
 *
 * @param components Blend components with extruder IDs and ratios.
 * @return Cycle of 1-based physical extruder IDs, or empty if there are no components.
 */
std::vector<unsigned int>
build_canonical_cycle(const Domain::VirtualExtruderComponents& components);

/**
 * @brief Even split of 100 percent over count components, the remainder going to the last one.
 *
 * @param count Number of components to split over.
 * @return Percent per component, summing to exactly 100. Empty when count is 0.
 */
std::vector<int> balanced_ratios_percent(size_t count);

/**
 * @brief Build a canonical cycle of physical extruder IDs from component ratios.
 *
 * For blends, filters components with ratio > 0 and quantizes their
 * ratios into a repeating sequence (e.g. ratios 2:1 produce [E1, E1, E2]).
 * For gradients, returns the first and last stop extruder IDs.
 *
 * @param virtual_extruder Virtual extruder to build the cycle for.
 * @return Cycle of 1-based physical extruder IDs, or empty if unresolvable.
 */
std::vector<unsigned int> build_sequence(const Domain::VirtualExtruder& virtual_extruder);

/**
 * @brief Remap virtual extruder IDs and mm-painting data onto a printer with more slots.
 *
 * Shifts colliding virtual IDs above the target physical range,
 * then remaps TriangleSelector states on all painted ModelVolumes of the given objects.
 *
 * @param objects Objects of the printer group whose volumes are remapped in-place.
 * @param target_virtual_extruders Virtual extruder list to update IDs on.
 * @param target_physical_count Physical extruder count of the current printer.
 * @param source_physical_count Slot count the definitions were authored for, 0 skips the remap.
 */
void remap_virtual_extruders_on_import(
    const std::vector<Domain::ModelObject*>& objects,
    Domain::VirtualExtruders& target_virtual_extruders,
    unsigned int target_physical_count,
    unsigned int source_physical_count
);

} // namespace Slic3r::Biz::Algorithms::VirtualExtruder
