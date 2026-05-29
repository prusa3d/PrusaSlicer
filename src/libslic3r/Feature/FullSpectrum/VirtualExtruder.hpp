#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
class Model;
class PrintObject;
class PrintRegionConfig;
class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;
} // namespace Slic3r

namespace Slic3r::FullSpectrum {

inline constexpr size_t MAX_BLEND_COMPONENTS = 3;

struct VirtualExtruderComponent
{
    unsigned int extruder_id;
    double ratio;

    bool operator==(const VirtualExtruderComponent& other) const
    {
        return this->extruder_id == other.extruder_id && this->ratio == other.ratio;
    }

    bool operator!=(const VirtualExtruderComponent& other) const
    {
        return !(*this == other);
    }
};

using VirtualExtruderComponents = std::vector<VirtualExtruderComponent>;

struct VirtualExtruderGradientStop
{
    unsigned int extruder_id;
    double position;

    bool operator==(const VirtualExtruderGradientStop& other) const
    {
        return this->extruder_id == other.extruder_id && this->position == other.position;
    }

    bool operator!=(const VirtualExtruderGradientStop& other) const
    {
        return !(*this == other);
    }
};

using VirtualExtruderGradientStops = std::vector<VirtualExtruderGradientStop>;

struct VirtualExtruderGradient
{
    std::optional<double> z_min;
    std::optional<double> z_max;
    std::vector<VirtualExtruderGradientStop> stops;

    bool operator==(const VirtualExtruderGradient& other) const
    {
        return this->z_min == other.z_min
            && this->z_max == other.z_max
            && this->stops == other.stops;
    }

    bool operator!=(const VirtualExtruderGradient& other) const
    {
        return !(*this == other);
    }
};

struct VirtualExtruder
{
    enum class Type
    {
        Blend,
        Gradient,
    };

    unsigned int id;
    std::optional<std::string> color;
    VirtualExtruderComponents components;
    std::optional<VirtualExtruderGradient> gradient;

    Type type() const
    {
        return gradient.has_value() ? Type::Gradient : Type::Blend;
    }

    /**
     * @brief Compute a display color for this virtual extruder.
     *
     * Returns the user-assigned color if set. Otherwise blends
     * physical extruder colors by component ratios (blend) or by
     * gradient stop coverage (gradient).
     *
     * @param physical_extruders_colors_0based 0-based hex color per physical extruder.
     * @return Hex color string (e.g. "#AA5500").
     */
    std::string effective_color(
        const std::vector<std::string>& physical_extruders_colors_0based
    ) const;

    bool operator==(const VirtualExtruder& other) const
    {
        return this->id == other.id
            && this->color == other.color
            && this->components == other.components
            && this->gradient == other.gradient;
    }

    bool operator!=(const VirtualExtruder& other) const
    {
        return !(*this == other);
    }

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
        const std::vector<double>& print_z_per_layer
    ) const;

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
        const std::vector<double>& print_z_per_layer,
        const std::vector<std::pair<double, double>>& z_ranges
    ) const;

    /**
     * @brief Build a canonical cycle of physical extruder IDs from component ratios.
     *
     * For blends, filters components with ratio > 0 and quantizes their
     * ratios into a repeating sequence (e.g. ratios 2:1 produce [E1, E1, E2]).
     * For gradients, returns the first and last stop extruder IDs.
     *
     * @return Cycle of 1-based physical extruder IDs, or empty if unresolvable.
     */
    std::vector<unsigned int> build_sequence() const;
};

using VirtualExtruders = std::vector<VirtualExtruder>;

/**
 * @brief Check whether a 1-based extruder ID belongs to a virtual extruder.
 */
bool
is_virtual_extruder(unsigned int extruder_id_1based, const VirtualExtruders& virtual_extruders);

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
VirtualExtruders normalize_virtual_extruders(const VirtualExtruders& raw_virtual_extruders);

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
VirtualExtruders filter_virtual_extruders_for_physical_count(
    unsigned int num_physical,
    const VirtualExtruders& normalized_virtual_extruders
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
    const VirtualExtruders& virtual_extruders
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
    const VirtualExtruders& virtual_extruders
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
    const VirtualExtruderComponents& components,
    const std::vector<std::string>& physical_colors
);

/**
 * @brief Check if a PrintRegionConfig's perimeter extruder is a virtual extruder.
 *
 * @return The virtual extruder ID if it matches, or std::nullopt.
 */
std::optional<unsigned int> source_virtual_extruder_in_region_config(
    const PrintRegionConfig& config,
    const VirtualExtruders& virtual_extruders
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
    const VirtualExtruders& virtual_extruders
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
    std::vector<std::vector<ExPolygons>>& segmentation,
    const std::vector<double>& print_z_per_layer,
    unsigned int num_physical_extruders,
    const VirtualExtruders& virtual_extruders
);

std::string serialize_virtual_extruders_to_json(
    const std::vector<std::string>& physical_extruders_colors,
    const VirtualExtruders& virtual_extruders
);

/**
 * @brief All data parsed from the Full Spectrum JSON in a 3MF.
 */
struct FullSpectrumConfig
{
    VirtualExtruders virtual_extruders;
    // Number of physical extruders the 3MF was saved with (0 = absent/old format).
    unsigned int source_physical_count = 0;
    // Hex color per physical extruder, 0-based. Empty when not present in JSON.
    std::vector<std::string> physical_colors;
};

FullSpectrumConfig deserialize_virtual_extruders_from_json(const std::string& json_content);

/**
 * @brief Remap virtual extruder IDs and mm-painting data on 3MF import.
 *
 * Shifts colliding virtual IDs above the target physical range,
 * then remaps TriangleSelector states on all painted ModelVolumes.
 *
 * @param loaded_model Model whose volumes are remapped in-place.
 * @param target_virtual_extruders Virtual extruder list to update IDs on.
 * @param target_physical_count Physical extruder count of the current printer.
 * @param fs_config Parsed Full Spectrum config from the 3MF.
 */
void remap_full_spectrum_on_import(
    Model& loaded_model,
    VirtualExtruders& target_virtual_extruders,
    unsigned int target_physical_count,
    const FullSpectrumConfig& fs_config
);

} // namespace Slic3r::FullSpectrum
