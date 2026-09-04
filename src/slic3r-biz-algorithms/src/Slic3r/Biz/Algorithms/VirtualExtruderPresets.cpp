#include "Slic3r/Biz/Algorithms/VirtualExtruderPresets.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"

#include <prusa_fdm_mixer/prusa_fdm_mixer.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <tuple>
#include <utility>

namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets {

namespace {

constexpr const char* FALLBACK_COLOR          = "#808080";
constexpr double PRESET_DEDUPE_DELTA_E        = 5.0;
constexpr std::size_t MAX_THREE_COLOR_PRESETS = 200;
constexpr double HUE_BUCKET_DEGREES           = 20.0;
constexpr double ACHROMATIC_CHROMA            = 5.0;

/**
 * @brief Whether the slot can take part in a preset at all.
 *
 * @param slots               Physical extruder slots, 0-based.
 * @param extruder_id_1based  Slot to test, 1-based.
 */
bool is_slot_enabled(const PhysicalExtruderSlots& slots, unsigned int extruder_id_1based)
{
    return extruder_id_1based >= 1
        && extruder_id_1based <= slots.size()
        && slots[extruder_id_1based - 1].enabled;
}

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
        if (!is_slot_enabled(slots, first)) {
            continue;
        }

        for (unsigned int second = first + 1; second <= slot_count; ++second) {
            if (!is_slot_enabled(slots, second)) {
                continue;
            }

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
        if (!is_slot_enabled(slots, first)) {
            continue;
        }

        for (unsigned int second = first + 1; second <= slot_count; ++second) {
            if (!is_slot_enabled(slots, second)) {
                continue;
            }

            for (unsigned int third = second + 1; third <= slot_count; ++third) {
                if (!is_slot_enabled(slots, third)) {
                    continue;
                }

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

                if (presets.size() >= MAX_THREE_COLOR_PRESETS) {
                    return presets;
                }
            }
        }
    }

    return presets;
}

/**
 * @brief Perceptual color of the recipe, or std::nullopt when it cannot be computed.
 */
std::optional<prusa_fdm_mixer::LAB>
preset_lab(const BlendPreset& preset, const PhysicalExtruderSlots& slots)
{
    try {
        return prusa_fdm_mixer::rgb_to_lab(
            prusa_fdm_mixer::hex_to_rgb(preset_color(preset, slots))
        );
    } catch (...) {
        return std::nullopt;
    }
}

/**
 * @brief Sort key that lays the presets out as a palette.
 *
 * Chromatic colors come first, grouped into hue buckets and sorted by lightness inside
 * each bucket. The achromatic ones follow, sorted by lightness alone.
 */
std::tuple<int, int, double> preset_sort_key(const prusa_fdm_mixer::LAB& lab)
{
    const double chroma = std::sqrt(lab.a * lab.a + lab.b * lab.b);
    if (chroma < ACHROMATIC_CHROMA) {
        return {1, 0, lab.L};
    }

    double hue_degrees = std::atan2(lab.b, lab.a) * 180.0 / std::numbers::pi;
    if (hue_degrees < 0.0) {
        hue_degrees += 360.0;
    }

    return {0, static_cast<int>(hue_degrees / HUE_BUCKET_DEGREES), lab.L};
}

struct AcceptedPreset
{
    BlendPreset preset;
    prusa_fdm_mixer::LAB lab;
};

BlendPresets sorted_presets(std::vector<AcceptedPreset> accepted_presets)
{
    std::vector<std::pair<std::tuple<int, int, double>, BlendPreset>> decorated_presets;
    decorated_presets.reserve(accepted_presets.size());
    for (AcceptedPreset& accepted_preset : accepted_presets) {
        decorated_presets
            .emplace_back(preset_sort_key(accepted_preset.lab), std::move(accepted_preset.preset));
    }

    std::ranges::stable_sort(
        decorated_presets,
        [](const auto& left, const auto& right) { return left.first < right.first; }
    );

    BlendPresets presets;
    presets.reserve(decorated_presets.size());
    for (auto& decorated_preset : decorated_presets) {
        presets.push_back(std::move(decorated_preset.second));
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

BlendPresetGroups build_blend_presets(const PhysicalExtruderSlots& slots)
{
    BlendPresets two_color_presets   = build_two_color_presets(slots);
    BlendPresets three_color_presets = build_three_color_presets(slots);

    // Deduplicate against everything accepted so far, two-color recipes first.
    // When a pair and a triple predict the same color, the simpler recipe is the one worth offering.
    std::vector<AcceptedPreset> accepted_two_color;
    std::vector<AcceptedPreset> accepted_three_color;
    std::vector<prusa_fdm_mixer::LAB> accepted_labs;
    accepted_labs.reserve(two_color_presets.size() + three_color_presets.size());

    const auto accept_distinct_presets =
        [&](BlendPresets& presets, std::vector<AcceptedPreset>& accepted_presets)
    {
        for (BlendPreset& preset : presets) {
            const std::optional<prusa_fdm_mixer::LAB> lab = preset_lab(preset, slots);
            if (!lab.has_value()) {
                continue;
            }

            const bool too_close = std::ranges::any_of(
                accepted_labs,
                [&lab](const prusa_fdm_mixer::LAB& accepted_lab)
                {
                    return prusa_fdm_mixer::delta_e_2000(*lab, accepted_lab)
                        < PRESET_DEDUPE_DELTA_E;
                }
            );
            if (too_close) {
                continue;
            }

            accepted_labs.push_back(*lab);
            accepted_presets.push_back({std::move(preset), *lab});
        }
    };

    accept_distinct_presets(two_color_presets, accepted_two_color);
    accept_distinct_presets(three_color_presets, accepted_three_color);

    return {
        sorted_presets(std::move(accepted_two_color)),
        sorted_presets(std::move(accepted_three_color))
    };
}

} // namespace Slic3r::Biz::Algorithms::VirtualExtruderPresets
