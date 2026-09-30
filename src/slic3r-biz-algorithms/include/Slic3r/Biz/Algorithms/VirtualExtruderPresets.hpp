#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets {

struct PhysicalExtruderSlot
{
    std::string hex_color;
    std::string filament_type;
};

using PhysicalExtruderSlots = std::vector<PhysicalExtruderSlot>;

/**
 * @brief One offered blend recipe.
 *
 * Both vectors have the same length, two entries for a two-color blend and three for a
 * three-color one. The ratios are whole percent and sum to 100.
 */
struct BlendPreset
{
    std::vector<unsigned int> extruder_ids_1based;
    std::vector<int> ratios_percent;
};

using BlendPresets = std::vector<BlendPreset>;

/**
 * @brief Perceptual color of a recipe.
 */
struct PerceptualColor
{
    double lightness{0.0};
    double a{0.0};
    double b{0.0};
};

/**
 * @brief Build every blend candidate the slots can form, without deduplication.
 *
 * Pairs and triples of slots that share the same non-empty filament type, in the
 * ratios 50/50, 75/25 and 25/75 for pairs, and for triples the balanced 33/33/34 plus
 * 50/25/25 with each component dominant in turn. Pairs come first.
 *
 * @param slots Physical extruder slots, 0-based.
 */
BlendPresets build_blend_candidates(const PhysicalExtruderSlots& slots);

/**
 * @brief Perceptual color of the mixed recipe, or std::nullopt when it cannot be mixed.
 */
std::optional<PerceptualColor>
preset_perceptual_color(const BlendPreset& preset, const PhysicalExtruderSlots& slots);

/**
 * @brief Perceptual difference of two colors.
 */
double perceptual_distance(const PerceptualColor& lhs, const PerceptualColor& rhs);

/**
 * @brief Predicted color of the mixed recipe.
 *
 * @param preset Recipe to mix.
 * @param slots  Physical extruder slots the recipe refers to.
 * @return Hex color, or a neutral gray when the recipe cannot be mixed.
 */
std::string preset_color(const BlendPreset& preset, const PhysicalExtruderSlots& slots);

} // namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets
