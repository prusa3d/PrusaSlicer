#include "Slic3r/App/ColorMix/BlendPresetSortFilter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
#include <utility>

using Slic3r::Biz::Algorithms::VirtualExtruderPresets::PerceptualColor;

namespace Slic3r::App::ColorMix {

const constexpr double MIN_DISTINCT_DELTA_E = 5.0;
const constexpr double ACHROMATIC_CHROMA    = 5.0;
const constexpr double HUE_BUCKET_DEGREES   = 20.0;

namespace {

/**
 * @brief Sort key that lays the presets out as a palette.
 *
 * Chromatic colors come first, grouped into hue buckets and sorted by lightness inside
 * each bucket. The achromatic ones follow, sorted by lightness alone.
 */
std::tuple<int, int, double> palette_sort_key(const PerceptualColor& color)
{
    const double chroma = std::sqrt(color.a * color.a + color.b * color.b);
    if (chroma < ACHROMATIC_CHROMA) {
        return {1, 0, color.lightness};
    }

    double hue_degrees = std::atan2(color.b, color.a) * 180.0 / std::numbers::pi;
    if (hue_degrees < 0.0) {
        hue_degrees += 360.0;
    }

    return {0, static_cast<int>(hue_degrees / HUE_BUCKET_DEGREES), color.lightness};
}

} // namespace

BlendPresetSortFilter::BlendPresetSortFilter(FnExtruderEnabled is_extruder_enabled) :
    m_is_extruder_enabled(std::move(is_extruder_enabled))
{
    this->set_filter_fn(
        [this](const BlendTileData& tile) -> bool
        { return std::ranges::all_of(tile.preset.extruder_ids_1based, m_is_extruder_enabled); }
    );
    this->set_group_by_fn(
        [this](const BlendTileData& tile, std::unordered_set<size_t>& accepted_indexes) -> bool
        {
            if (!tile.perceptual_color.has_value()) {
                return true;
            }

            for (const size_t accepted_index : accepted_indexes) {
                const BlendTileData& accepted = this->source_model()->at(accepted_index);
                if (perceptual_distance(*tile.perceptual_color, *accepted.perceptual_color)
                    < MIN_DISTINCT_DELTA_E)
                {
                    return true;
                }
            }

            accepted_indexes.insert(tile.candidate_index);
            return false;
        }
    );
    this->set_sort_fn(
        [](const BlendTileData& lhs, const BlendTileData& rhs) -> bool
        {
            const auto lhs_key = std::make_tuple(
                lhs.preset.extruder_ids_1based.size(),
                palette_sort_key(*lhs.perceptual_color),
                lhs.candidate_index
            );
            const auto rhs_key = std::make_tuple(
                rhs.preset.extruder_ids_1based.size(),
                palette_sort_key(*rhs.perceptual_color),
                rhs.candidate_index
            );

            return lhs_key < rhs_key;
        }
    );
}

} // namespace Slic3r::App::ColorMix
