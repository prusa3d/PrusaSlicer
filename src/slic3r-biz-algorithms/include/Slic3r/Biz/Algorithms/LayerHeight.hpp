#pragma once

#include "Slic3r/Domain/LayerHeightProfile.hpp"

#include <vector>

namespace Slic3r::Domain {
class ModelObject;
class ConfigView;
struct ConfigPackFDM;
} // namespace Slic3r::Domain

namespace Slic3r::Biz::Algorithms::LayerHeight {

/**
 * @brief Minimum layer height for the variable layer height algorithm.
 *
 * @param idx_nozzle Nozzle index is 1-based.
 */
double min_layer_height_from_nozzle(const Domain::ConfigView& config, int idx_nozzle);

/**
 * @brief Maximum layer height for the variable layer height algorithm, 3/4 of a nozzle diameter, by default,
 * it should not be smaller than the minimum layer height.
 *
 * @param idx_nozzle Nozzle index is 1-based.
 */
double max_layer_height_from_nozzle(const Domain::ConfigView& config, int idx_nozzle);

/**
 * @brief Minimum layer height for the variable layer height algorithm.
 *
 * @param idx_nozzle Nozzle index is 1-based.
 */
double min_layer_height_from_nozzle(const Domain::ConfigPackFDM& config, int idx_nozzle);

/**
 * @brief Maximum layer height for the variable layer height algorithm, 3/4 of a nozzle diameter, by default,
 * it should not be smaller than the minimum layer height.
 *
 * @param idx_nozzle Nozzle index is 1-based.
 */
double max_layer_height_from_nozzle(const Domain::ConfigPackFDM& config, int idx_nozzle);

enum class AdjustAction : unsigned int
{
    Increase = 0,
    Decrease = 1,
    Reduce   = 2,
    Smooth   = 3
};

struct AdjustParams
{
    /**
     * Minimum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double min_layer_height{0.};
    /**
     * Maximum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double max_layer_height{0.};
    /**
     * The regular layer height, applied for all but the first layer, if not overridden by layer ranges
     * or by the variable layer thickness table.
     */
    double layer_height{0.};
    /**
     * Thickness of the first layer. This is either the first print layer thickness if printed without a raft,
     * or a bridging flow thickness if printed over a non-soluble raft,
     * or a normal layer height if printed over a soluble raft.
     */
    double first_object_layer_height{0.};
    /**
     * Height of the object to be printed. This value does not contain the raft height.
     * This value isn't scaled by shrinkage compensation in the Z-axis.
     */
    double object_print_z_uncompensated_height{0.};
    /**
     * Is the 1st object layer height fixed, or could it be varied?
     */
    bool first_object_layer_height_fixed{false};
};

/**
 * @brief Adjusts layer height profile at the specified Z position.
 */
void adjust_layer_height_profile(
    const AdjustParams& params,
    Domain::ZHeightPairs& layer_height_profile,
    double z,
    double layer_thickness_delta,
    double band_width,
    AdjustAction action
);

struct SmoothParams
{
    /**
     * Minimum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double min_layer_height{0.};
    /**
     * Maximum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double max_layer_height{0.};
    /**
     * The regular layer height, applied for all but the first layer, if not overridden by layer ranges
     * or by the variable layer thickness table.
     */
    double layer_height{0.};
    /**
     * Is the 1st object layer height fixed, or could it be varied?
     */
    bool first_object_layer_height_fixed{false};
    /**
     * Gaussian blur radius.
     */
    unsigned int radius{5};
    bool keep_min{false};
};

/**
 * @brief Applies Gaussian smoothing to the layer height profile.
 */
Domain::ZHeightPairs
smooth_height_profile(const Domain::ZHeightPairs& profile, const SmoothParams& params);

struct GenerateLayersParams
{
    /**
     * Minimum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double min_layer_height{0.};
    /**
     * Maximum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double max_layer_height{0.};
    /**
     * Thickness of the first layer. This is either the first print layer thickness if printed without a raft,
     * or a bridging flow thickness if printed over a non-soluble raft,
     * or a normal layer height if printed over a soluble raft.
     */
    double first_object_layer_height{0.};
    /**
     * Height of the object to be printed. This value does not contain the raft height.
     * This value is scaled by shrinkage compensation in the Z-axis.
     */
    double object_print_z_height{0.};
    /**
     * Scaling factor for compensating shrinkage in Z-axis.
     */
    double object_shrinkage_compensation_z = {1.};
    /**
     * Is the 1st object layer height fixed, or could it be varied?
     */
    bool first_object_layer_height_fixed{false};
};

/**
 * @brief Produce object layers as pairs of low / high layer boundaries, stored into a linear vector.
 * The object layers are based at z=0, ignoring the raft layers.
 *
 * @return Vector of LayerZRange, each representing one layer's bottom and top Z boundaries.
 */
Domain::LayerZRanges generate_object_layers(
    const GenerateLayersParams& params,
    const Domain::ZHeightPairs& layer_height_profile
);

struct AdaptiveParams
{
    /**
     * Minimum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double min_layer_height{0.};
    /**
     * Maximum layer height, to be used for the automatic adaptive layer height algorithm,
     * or by an interactive layer height editor.
     */
    double max_layer_height{0.};
    /**
     * Thickness of the first layer. This is either the first print layer thickness if printed without a raft,
     * or a bridging flow thickness if printed over a non-soluble raft,
     * or a normal layer height if printed over a soluble raft.
     */
    double first_object_layer_height{0.};
    /**
     * The regular layer height, applied for all but the first layer, if not overridden by layer ranges
     * or by the variable layer thickness table.
     */
    double layer_height{0.};
    /**
     * Height of the object to be printed. This value does not contain the raft height.
     * This value isn't scaled by shrinkage compensation in the Z-axis.
     */
    double object_print_z_uncompensated_height{0.};
    /**
     * Is the 1st object layer height fixed, or could it be varied?
     */
    bool first_object_layer_height_fixed{false};
};

/**
 * @brief Generates adaptive layer height profile based on mesh geometry.
 */
Domain::ZHeightPairs layer_height_profile_adaptive(
    const AdaptiveParams& params,
    const Domain::ModelObject& object,
    float quality_factor
);

struct ProfileFromRangesParams
{
    /**
     * The regular layer height, applied for all but the first layer, if not overridden by layer ranges
     * or by the variable layer thickness table.
     */
    double layer_height{0.};
    /**
     * Thickness of the first layer. This is either the first print layer thickness if printed without a raft,
     * or a bridging flow thickness if printed over a non-soluble raft,
     * or a normal layer height if printed over a soluble raft.
     */
    double first_object_layer_height{0.};
    /**
     * Height of the object to be printed. This value does not contain the raft height.
     * This value is scaled by shrinkage compensation in the Z-axis.
     */
    double object_print_z_height{0.};
    /**
     * Height of the object to be printed. This value does not contain the raft height.
     * This value isn't scaled by shrinkage compensation in the Z-axis.
     */
    double object_print_z_uncompensated_height{0.};
    /**
     * Is the 1st object layer height fixed, or could it be varied?
     */
    bool first_object_layer_height_fixed{false};
};

/**
 * @brief Convert layer_config_ranges to layer_height_profile.
 *
 * Both are referenced to z=0, meaning the raft layers are not accounted for
 * in the height profile and the printed object may be lifted by the raft
 * thickness at the time of the G-code generation.
 */
Domain::ZHeightPairs layer_height_profile_from_ranges(
    const ProfileFromRangesParams& params,
    const Domain::LayerConfigRanges& layer_config_ranges
);

/**
 * @brief Check whether the layer height profile describes a fixed layer height profile.
 *
 * @param layer_height The regular layer height, applied for all but the first layer, if not
 *        overridden by layer ranges or by the variable layer thickness table.
 * @param first_object_layer_height Thickness of the first layer. This is either the first print
 *        layer thickness if printed without a raft, or a bridging flow thickness if printed over
 *        a non-soluble raft, or a normal layer height if printed over a soluble raft.
 * @param layer_height_profile The layer height profile to check.
 * @return true if the profile describes a fixed (non-variable) layer height, false otherwise.
 */
bool check_object_layers_fixed(
    double layer_height,
    double first_object_layer_height,
    const Domain::ZHeightPairs& layer_height_profile
);

/**
 * @brief Clamps all layer heights in the profile to the given min/max bounds.
 *
 * @param layer_height_profile The layer height profile to clamp in place.
 * @param min_layer_height The minimum allowed layer height.
 * @param max_layer_height The maximum allowed layer height.
 */
void clamp_layer_height_profile(
    Domain::ZHeightPairs& layer_height_profile,
    double min_layer_height,
    double max_layer_height
);

} // namespace Slic3r::Biz::Algorithms::LayerHeight
