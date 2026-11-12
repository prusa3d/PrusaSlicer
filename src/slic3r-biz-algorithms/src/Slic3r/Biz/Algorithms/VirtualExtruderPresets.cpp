#include "Slic3r/Biz/Algorithms/VirtualExtruderPresets.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"

#include <prusa_fdm_mixer/prusa_fdm_mixer.hpp>

#include <iterator>
#include <optional>
#include <utility>

namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets {

namespace {

constexpr const char* FALLBACK_COLOR = "#808080";

/**
 * @brief Whether all given slots carry the same known filament type.
 *
 * Mixing filaments of different types is not offered, because they usually cannot be
 * printed with the same temperature and the predicted color would be misleading.
 */
bool have_same_filament_type(
    const PhysicalExtruderSlots& slots,
    const std::vector<unsigned int>& extruder_ids_1based
)
{
    const std::string& first_type = slots[extruder_ids_1based.front() - 1].filament_type;
    if (first_type.empty()) {
        return false;
    }

    for (const unsigned int extruder_id_1based : extruder_ids_1based) {
        if (slots[extruder_id_1based - 1].filament_type != first_type) {
            return false;
        }
    }

    return true;
}

BlendPresets build_two_color_presets(const PhysicalExtruderSlots& slots)
{
    const unsigned int slot_count = static_cast<unsigned int>(slots.size());

    BlendPresets presets;
    for (unsigned int first = 1; first <= slot_count; ++first) {
        for (unsigned int second = first + 1; second <= slot_count; ++second) {
            if (!have_same_filament_type(slots, {first, second})) {
                continue;
            }

            presets.push_back({{first, second}, {50, 50}});
            presets.push_back({{first, second}, {75, 25}});
            presets.push_back({{first, second}, {25, 75}});
        }
    }

    return presets;
}

BlendPresets build_three_color_presets(const PhysicalExtruderSlots& slots)
{
    const unsigned int slot_count = static_cast<unsigned int>(slots.size());

    BlendPresets presets;
    for (unsigned int first = 1; first <= slot_count; ++first) {
        for (unsigned int second = first + 1; second <= slot_count; ++second) {
            for (unsigned int third = second + 1; third <= slot_count; ++third) {
                if (!have_same_filament_type(slots, {first, second, third})) {
                    continue;
                }

                presets.push_back(
                    {{first, second, third}, VirtualExtruder::balanced_ratios_percent(3)}
                );

                for (std::size_t dominant = 0; dominant < 3; ++dominant) {
                    BlendPreset preset;
                    preset.extruder_ids_1based      = {first, second, third};
                    preset.ratios_percent           = {25, 25, 25};
                    preset.ratios_percent[dominant] = 50;
                    presets.push_back(std::move(preset));
                }
            }
        }
    }

    return presets;
}

} // namespace

std::string preset_color(const BlendPreset& preset, const PhysicalExtruderSlots& slots)
{
    std::vector<prusa_fdm_mixer::Part> parts;
    parts.reserve(preset.extruder_ids_1based.size());
    for (std::size_t i = 0; i < preset.extruder_ids_1based.size(); ++i) {
        const unsigned int extruder_id_1based = preset.extruder_ids_1based[i];
        if (extruder_id_1based >= 1 && extruder_id_1based <= slots.size()) {
            parts.push_back(
                {slots[extruder_id_1based - 1].hex_color,
                 static_cast<double>(preset.ratios_percent[i]) / 100.0}
            );
        }
    }

    if (parts.empty()) {
        return FALLBACK_COLOR;
    }

    try {
        return prusa_fdm_mixer::mix(parts);
    } catch (...) {
        return FALLBACK_COLOR;
    }
}

BlendPresets build_blend_candidates(const PhysicalExtruderSlots& slots)
{
    BlendPresets candidates             = build_two_color_presets(slots);
    BlendPresets three_color_candidates = build_three_color_presets(slots);

    candidates.insert(
        candidates.end(),
        std::make_move_iterator(three_color_candidates.begin()),
        std::make_move_iterator(three_color_candidates.end())
    );

    return candidates;
}

std::optional<PerceptualColor>
preset_perceptual_color(const BlendPreset& preset, const PhysicalExtruderSlots& slots)
{
    try {
        const prusa_fdm_mixer::LAB lab =
            prusa_fdm_mixer::rgb_to_lab(prusa_fdm_mixer::hex_to_rgb(preset_color(preset, slots)));
        return PerceptualColor{lab.L, lab.a, lab.b};
    } catch (...) {
        return std::nullopt;
    }
}

double perceptual_distance(const PerceptualColor& lhs, const PerceptualColor& rhs)
{
    return prusa_fdm_mixer::delta_e_2000(
        prusa_fdm_mixer::LAB{lhs.lightness, lhs.a, lhs.b},
        prusa_fdm_mixer::LAB{rhs.lightness, rhs.a, rhs.b}
    );
}

} // namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets
