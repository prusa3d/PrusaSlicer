#pragma once

#include <string>
#include <vector>

namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets {

struct PhysicalExtruderSlot
{
    std::string hex_color;
    std::string filament_type;
    bool enabled{true};
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

struct BlendPresetGroups
{
    BlendPresets two_color;
    BlendPresets three_color;
};

/**
 * @brief Build the blend recipes offered for the given slots.
 *
 * Pairs and triples of enabled slots that share the same non-empty filament type, in the
 * ratios 50/50, 75/25 and 25/75 for pairs, and for triples the balanced 33/33/34 plus
 * 50/25/25 with each component dominant in turn.
 * Recipes whose predicted color is perceptually indistinguishable from one already in the
 * offer are dropped, and each section is sorted so that it reads as a palette: by hue and
 * then by lightness, with the achromatic ones last.
 *
 * @param slots Physical extruder slots, 0-based.
 * @return The deduplicated and sorted offer.
 */
BlendPresetGroups build_blend_presets(const PhysicalExtruderSlots& slots);

/**
 * @brief Predicted color of the mixed recipe.
 *
 * @param preset Recipe to mix.
 * @param slots  Physical extruder slots the recipe refers to.
 * @return Hex color, or a neutral gray when the recipe cannot be mixed.
 */
std::string preset_color(const BlendPreset& preset, const PhysicalExtruderSlots& slots);

} // namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets
