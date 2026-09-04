#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"

#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/TriangleSelector.hpp"
#include "Slic3r/Log.hpp"

#include <prusa_fdm_mixer/prusa_fdm_mixer.hpp>

#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r::Biz::Algorithms::VirtualExtruder {

using Slic3r::Domain::MAX_BLEND_COMPONENTS;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruderComponent;
using Slic3r::Domain::VirtualExtruderComponents;
using Slic3r::Domain::VirtualExtruderGradient;
using Slic3r::Domain::VirtualExtruderGradientStop;
using Slic3r::Domain::VirtualExtruderGradientStops;
using Slic3r::Domain::VirtualExtruders;
using Slic3r::Domain::TriangleSelector::TRIANGLE_STATE_TYPE_COUNT;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

constexpr int BLEND_WEIGHT_RESOLUTION     = 64;
constexpr double BLEND_QUANTISE_MAX_ERROR = 0.03;

bool is_virtual_extruder(unsigned int extruder_id_1based, const VirtualExtruders& virtual_extruders)
{
    for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
        if (virtual_extruder.id == extruder_id_1based) {
            return true;
        }
    }

    return false;
}

VirtualExtruders normalize_virtual_extruders(const VirtualExtruders& raw_virtual_extruders)
{
    VirtualExtruders normalized_virtual_extruders;
    normalized_virtual_extruders.reserve(raw_virtual_extruders.size());
    for (const VirtualExtruder& raw_virtual_extruder : raw_virtual_extruders) {
        VirtualExtruder normalized_virtual_extruder;
        normalized_virtual_extruder.id    = raw_virtual_extruder.id;
        normalized_virtual_extruder.color = raw_virtual_extruder.color;

        if (raw_virtual_extruder.gradient.has_value()) {
            const VirtualExtruderGradient& raw_gradient = *raw_virtual_extruder.gradient;
            if (raw_gradient.z_min.has_value()
                && raw_gradient.z_max.has_value()
                && !(*raw_gradient.z_max > *raw_gradient.z_min))
            {
                SPDLOG_ERROR(
                    "VirtualExtruder id={} dropped: gradient z_max ({}) must be strictly greater "
                    "than z_min ({})",
                    raw_virtual_extruder.id,
                    *raw_gradient.z_max,
                    *raw_gradient.z_min
                );
                continue;
            }

            VirtualExtruderGradientStops sorted_stops = raw_gradient.stops;
            std::ranges::sort(
                sorted_stops,
                [](const VirtualExtruderGradientStop& a, const VirtualExtruderGradientStop& b)
                { return a.position < b.position; }
            );

            VirtualExtruderGradientStops deduped_stops;
            deduped_stops.reserve(sorted_stops.size());
            for (const VirtualExtruderGradientStop& stop : sorted_stops) {
                if (!(stop.position >= 0.0 && stop.position <= 1.0)) {
                    SPDLOG_ERROR(
                        "VirtualExtruder id={} gradient drops stop with position {} (must be in "
                        "[0, 1])",
                        raw_virtual_extruder.id,
                        stop.position
                    );
                    continue;
                }

                if (!deduped_stops.empty()
                    && std::abs(deduped_stops.back().position - stop.position) < 1e-9)
                {
                    SPDLOG_ERROR(
                        "VirtualExtruder id={} gradient drops duplicate-position stop at {}",
                        raw_virtual_extruder.id,
                        stop.position
                    );
                    continue;
                }

                deduped_stops.push_back(stop);
            }

            if (deduped_stops.size() < 2) {
                SPDLOG_ERROR(
                    "VirtualExtruder id={} dropped: gradient mode requires >= 2 distinct-position "
                    "stops, got {}",
                    raw_virtual_extruder.id,
                    deduped_stops.size()
                );
                continue;
            }

            std::set<unsigned int> distinct_extruders;
            for (const VirtualExtruderGradientStop& stop : deduped_stops) {
                distinct_extruders.insert(stop.extruder_id);
            }

            if (distinct_extruders.size() < 2) {
                SPDLOG_ERROR(
                    "VirtualExtruder id={} dropped: gradient stops reference fewer than 2 distinct "
                    "physical extruders",
                    raw_virtual_extruder.id
                );
                continue;
            }

            normalized_virtual_extruder.gradient = VirtualExtruderGradient{
                raw_gradient.z_min,
                raw_gradient.z_max,
                std::move(deduped_stops)
            };
            normalized_virtual_extruders.push_back(std::move(normalized_virtual_extruder));
            continue;
        }

        normalized_virtual_extruder.components.reserve(raw_virtual_extruder.components.size());
        double ratio_sum = 0;
        for (const VirtualExtruderComponent& component : raw_virtual_extruder.components) {
            if (component.ratio < 0.0) {
                SPDLOG_WARN(
                    "VirtualExtruder id={} drops component with negative ratio {}",
                    raw_virtual_extruder.id,
                    component.ratio
                );
                continue;
            }

            normalized_virtual_extruder.components.push_back(component);
            ratio_sum += component.ratio;
        }

        if (normalized_virtual_extruder.components.size() < 2) {
            SPDLOG_ERROR(
                "VirtualExtruder id={} dropped: fewer than 2 valid components remain",
                raw_virtual_extruder.id
            );
            continue;
        }

        if (normalized_virtual_extruder.components.size() > MAX_BLEND_COMPONENTS) {
            SPDLOG_ERROR(
                "VirtualExtruder id={} dropped: blend recipe has {} components (max {})",
                raw_virtual_extruder.id,
                normalized_virtual_extruder.components.size(),
                MAX_BLEND_COMPONENTS
            );
            continue;
        }

        if (std::abs(ratio_sum - 1.0) > 1e-6 && ratio_sum > 0) {
            for (VirtualExtruderComponent& component : normalized_virtual_extruder.components) {
                component.ratio /= ratio_sum;
            }
        }

        normalized_virtual_extruders.push_back(std::move(normalized_virtual_extruder));
    }

    return normalized_virtual_extruders;
}

VirtualExtruders filter_virtual_extruders_for_physical_count(
    unsigned int num_physical,
    const VirtualExtruders& normalized_virtual_extruders
)
{
    VirtualExtruders filtered_virtual_extruders;
    filtered_virtual_extruders.reserve(normalized_virtual_extruders.size());
    for (const VirtualExtruder& virtual_extruder : normalized_virtual_extruders) {
        if (virtual_extruder.gradient.has_value()) {
            bool all_stops_in_range = true;
            for (const VirtualExtruderGradientStop& stop : virtual_extruder.gradient->stops) {
                if (stop.extruder_id == 0 || stop.extruder_id > num_physical) {
                    SPDLOG_WARN(
                        "VirtualExtruder id={} dropped: gradient stop references extruder {} (only "
                        "{} physical extruders available)",
                        virtual_extruder.id,
                        stop.extruder_id,
                        num_physical
                    );
                    all_stops_in_range = false;
                    break;
                }
            }

            if (all_stops_in_range) {
                filtered_virtual_extruders.push_back(virtual_extruder);
            }

            continue;
        }

        bool all_components_in_range = true;
        for (const VirtualExtruderComponent& component : virtual_extruder.components) {
            if (component.extruder_id == 0 || component.extruder_id > num_physical) {
                SPDLOG_WARN(
                    "VirtualExtruder id={} dropped: component references extruder {} (only {} "
                    "physical extruders available)",
                    virtual_extruder.id,
                    component.extruder_id,
                    num_physical
                );
                all_components_in_range = false;
                break;
            }
        }

        if (all_components_in_range) {
            filtered_virtual_extruders.push_back(virtual_extruder);
        }
    }

    return filtered_virtual_extruders;
}

VirtualExtruders compatible_virtual_extruders(
    const VirtualExtruders& virtual_extruders,
    unsigned int physical_slot_count
)
{
    constexpr unsigned int max_virtual_extruder_id =
        static_cast<unsigned int>(TRIANGLE_STATE_TYPE_COUNT) - 1;

    // A blend needs at least two physical slots to be printable.
    if (physical_slot_count < 2) {
        return {};
    }

    VirtualExtruders id_valid_virtual_extruders;
    id_valid_virtual_extruders.reserve(virtual_extruders.size());
    std::set<unsigned int> seen_ids;
    for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
        // A virtual id must live above the physical slots and fit into a triangle state.
        if (virtual_extruder.id <= physical_slot_count
            || virtual_extruder.id > max_virtual_extruder_id)
        {
            SPDLOG_ERROR(
                "VirtualExtruder id={} dropped: id outside the valid range {}..{}",
                virtual_extruder.id,
                physical_slot_count + 1,
                max_virtual_extruder_id
            );
            continue;
        }

        if (!seen_ids.insert(virtual_extruder.id).second) {
            SPDLOG_ERROR(
                "VirtualExtruder id={} dropped: id collides with another virtual extruder",
                virtual_extruder.id
            );
            continue;
        }

        id_valid_virtual_extruders.push_back(virtual_extruder);
    }

    return filter_virtual_extruders_for_physical_count(
        physical_slot_count,
        id_valid_virtual_extruders
    );
}

/**
 * @brief Replaces each virtual extruder ID with its physical component IDs.
 * Non-virtual IDs pass through unchanged. Result is sorted and deduplicated.
 *
 * @param extruders   Input extruder IDs.
 * @param virtual_extruders    Virtual extruder definitions.
 * @param extruders_are_0based If true, IDs are 0-based; offset is applied internally.
 * @return Expanded, sorted, deduplicated list of physical extruder IDs.
 */
static std::vector<unsigned int> expand_virtual_extruders_impl(
    const std::vector<unsigned int>& extruders,
    const VirtualExtruders& virtual_extruders,
    const bool extruders_are_0based
)
{
    const unsigned int offset = extruders_are_0based ? 1u : 0u;

    std::vector<unsigned int> result;
    result.reserve(extruders.size());
    for (unsigned int extruder_id : extruders) {
        const unsigned int extruder_id_1based = extruder_id + offset;
        bool expanded                         = false;
        for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
            if (virtual_extruder.id == extruder_id_1based) {
                // Gradient mode: stops carry the physical references, components is unused.
                if (virtual_extruder.type() == VirtualExtruder::Type::Gradient) {
                    for (const VirtualExtruderGradientStop& stop : virtual_extruder.gradient->stops)
                    {
                        result.push_back(stop.extruder_id - offset);
                    }
                } else {
                    for (const VirtualExtruderComponent& component : virtual_extruder.components) {
                        result.push_back(component.extruder_id - offset);
                    }
                }

                expanded = true;
                break;
            }
        }

        if (!expanded) {
            result.push_back(extruder_id);
        }
    }

    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());

    return result;
}

std::vector<unsigned int> expand_virtual_extruders_1based(
    const std::vector<unsigned int>& extruders_1based,
    const VirtualExtruders& virtual_extruders
)
{
    return expand_virtual_extruders_impl(extruders_1based, virtual_extruders, false);
}

std::vector<unsigned int> expand_virtual_extruders_0based(
    const std::vector<unsigned int>& extruders_0based,
    const VirtualExtruders& virtual_extruders
)
{
    return expand_virtual_extruders_impl(extruders_0based, virtual_extruders, true);
}

std::string effective_color(
    const VirtualExtruder& virtual_extruder,
    const std::vector<std::string>& physical_extruders_colors_0based
)
{
    if (virtual_extruder.color.has_value()) {
        return *virtual_extruder.color;
    }

    if (virtual_extruder.gradient.has_value() && virtual_extruder.gradient->stops.size() >= 2) {
        const VirtualExtruderGradientStops& stops = virtual_extruder.gradient->stops;
        std::vector<VirtualExtruderComponent> weighted_blend;
        weighted_blend.reserve(stops.size());
        for (size_t i = 0; i < stops.size(); ++i) {
            const double lo = (i == 0) ? 0. : (stops[i - 1].position + stops[i].position) * 0.5;
            const double hi =
                (i == stops.size() - 1) ? 1. : (stops[i].position + stops[i + 1].position) * 0.5;
            weighted_blend.push_back({stops[i].extruder_id, std::max(0., hi - lo)});
        }

        return blend_virtual_extruder_color(weighted_blend, physical_extruders_colors_0based);
    }

    return blend_virtual_extruder_color(
        virtual_extruder.components,
        physical_extruders_colors_0based
    );
}

std::optional<Domain::ColorRGB> effective_color(
    const VirtualExtruder& virtual_extruder,
    const std::vector<Domain::ColorRGB>& physical_extruders_colors_0based
)
{
    std::vector<std::string> physical_hex_colors;
    physical_hex_colors.reserve(physical_extruders_colors_0based.size());
    for (const Domain::ColorRGB& color : physical_extruders_colors_0based) {
        physical_hex_colors.push_back(Algorithms::Color::encode_color(color));
    }

    const std::string effective_hex{effective_color(virtual_extruder, physical_hex_colors)};
    Domain::ColorRGB color;
    if (effective_hex.empty() || !Algorithms::Color::decode_color(effective_hex, color)) {
        return std::nullopt;
    }

    return color;
}

std::string blend_virtual_extruder_color(
    const VirtualExtruderComponents& components,
    const std::vector<std::string>& physical_colors
)
{
    std::vector<prusa_fdm_mixer::Part> parts;
    parts.reserve(components.size());
    for (const VirtualExtruderComponent& component : components) {
        if (component.extruder_id >= 1
            && component.extruder_id <= physical_colors.size()
            && component.ratio > 0.)
        {
            parts.push_back({physical_colors[component.extruder_id - 1], component.ratio});
        }
    }

    if (parts.empty()) {
        return {};
    }

    return prusa_fdm_mixer::mix(parts);
}

namespace {

/**
 * @brief Convert component ratios into the shortest integer counts
 *        that approximate them within BLEND_QUANTISE_MAX_ERROR.
 *
 * Tries increasing cycle lengths (2..BLEND_WEIGHT_RESOLUTION) until
 * the per-component ratio error is acceptable, then reduces by GCD.
 * E.g. ratios 0.66:0.33 may yield counts [2, 1].
 *
 * @param components Blend components with ratios.
 * @return Integer count per component defining the cycle length.
 */
std::vector<int> quantise_component_counts(const VirtualExtruderComponents& components)
{
    double total_ratio = 0.;
    for (const VirtualExtruderComponent& component : components) {
        total_ratio += component.ratio;
    }

    std::vector<int> counts_per_component;
    counts_per_component.reserve(components.size());

    for (int cycle_candidate = 2; cycle_candidate <= BLEND_WEIGHT_RESOLUTION; ++cycle_candidate) {
        counts_per_component.clear();
        for (const VirtualExtruderComponent& component : components) {
            counts_per_component.push_back(
                std::max(
                    1,
                    static_cast<int>(std::round((component.ratio / total_ratio) * cycle_candidate))
                )
            );
        }

        const int sum_of_counts =
            std::accumulate(counts_per_component.begin(), counts_per_component.end(), 0);

        double max_ratio_error = 0.;
        for (size_t i = 0; i < components.size(); ++i) {
            const double target_ratio = components[i].ratio / total_ratio;
            const double actual_ratio =
                static_cast<double>(counts_per_component[i]) / static_cast<double>(sum_of_counts);
            max_ratio_error = std::max(max_ratio_error, std::abs(target_ratio - actual_ratio));
        }

        if (max_ratio_error <= BLEND_QUANTISE_MAX_ERROR) {
            break;
        }
    }

    int gcd_factor = counts_per_component[0];
    for (size_t i = 1; i < counts_per_component.size(); ++i) {
        gcd_factor = std::gcd(gcd_factor, counts_per_component[i]);
    }

    if (gcd_factor > 1) {
        for (int& count : counts_per_component) {
            count /= gcd_factor;
        }
    }

    return counts_per_component;
}

} // namespace

std::vector<int> balanced_ratios_percent(const size_t count)
{
    if (count == 0) {
        return {};
    }

    const int even_percent = static_cast<int>(100 / count);
    std::vector<int> ratios_percent(count, even_percent);
    ratios_percent.back() += 100 - even_percent * static_cast<int>(count);

    return ratios_percent;
}

std::vector<unsigned int> build_canonical_cycle(const VirtualExtruderComponents& components)
{
    if (components.empty()) {
        return {};
    }

    if (components.size() == 1) {
        return {components[0].extruder_id};
    }

    const std::vector<int> counts_per_component = quantise_component_counts(components);
    const int cycle_length =
        std::accumulate(counts_per_component.begin(), counts_per_component.end(), 0);

    std::vector<unsigned int> sequence;
    sequence.reserve(static_cast<size_t>(cycle_length));
    std::vector<int> emitted_per_component(counts_per_component.size(), 0);
    for (int slot_index = 0; slot_index < cycle_length; ++slot_index) {
        size_t best_component_index = 0;
        double best_deficit         = -std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < counts_per_component.size(); ++i) {
            const double ideal_count_at_this_slot =
                static_cast<double>((slot_index + 1) * counts_per_component[i])
                / static_cast<double>(cycle_length);
            const double deficit =
                ideal_count_at_this_slot - static_cast<double>(emitted_per_component[i]);
            if (deficit > best_deficit) {
                best_deficit         = deficit;
                best_component_index = i;
            }
        }

        ++emitted_per_component[best_component_index];
        sequence.push_back(components[best_component_index].extruder_id);
    }

    return sequence;
}

std::vector<unsigned int> build_sequence(const VirtualExtruder& virtual_extruder)
{
    assert(!virtual_extruder.gradient.has_value());

    if (virtual_extruder.gradient.has_value()) {
        const std::vector<VirtualExtruderGradientStop>& stops = virtual_extruder.gradient->stops;
        if (stops.size() < 2) {
            return {};
        }

        return {stops.front().extruder_id, stops.back().extruder_id};
    }

    VirtualExtruderComponents active;
    active.reserve(virtual_extruder.components.size());
    for (const VirtualExtruderComponent& component : virtual_extruder.components) {
        if (component.ratio > 0.0) {
            active.push_back(component);
        }
    }

    return build_canonical_cycle(active);
}

/**
 * @brief Compute a remap table {old_id -> new_id} for virtual extruder IDs
 *        that collide with physical extruder slots on the target printer.
 *
 * @return Non-empty map when remapping is needed; empty otherwise.
 */
static std::map<unsigned int, unsigned int> compute_virtual_id_remap(
    unsigned int source_physical_count,
    unsigned int target_physical_count,
    const VirtualExtruders& virtual_extruders
)
{
    constexpr unsigned int max_id = static_cast<unsigned int>(TRIANGLE_STATE_TYPE_COUNT) - 1;

    if (source_physical_count == 0
        || source_physical_count >= target_physical_count
        || virtual_extruders.empty())
    {
        return {};
    }

    bool any_collision = false;
    for (const VirtualExtruder& ve : virtual_extruders) {
        if (ve.id >= 1 && ve.id <= target_physical_count) {
            any_collision = true;
            break;
        }
    }

    if (!any_collision) {
        return {};
    }

    const unsigned int shift = target_physical_count - source_physical_count;
    std::map<unsigned int, unsigned int> remap;
    for (const VirtualExtruder& ve : virtual_extruders) {
        const unsigned int new_id = ve.id + shift;
        if (new_id > max_id) {
            SPDLOG_ERROR(
                "VirtualExtruder: remap would push virtual extruder id={} to {} which exceeds max "
                "{}, aborting remap",
                ve.id,
                new_id,
                max_id
            );
            return {};
        }

        remap[ve.id] = new_id;
    }

    return remap;
}

void remap_virtual_extruders_on_import(
    const std::vector<ModelObject*>& objects,
    VirtualExtruders& target_virtual_extruders,
    unsigned int target_physical_count,
    unsigned int source_physical_count
)
{
    if (source_physical_count == 0 || target_virtual_extruders.empty()) {
        return;
    }

    const std::map<unsigned int, unsigned int> id_remap = compute_virtual_id_remap(
        source_physical_count,
        target_physical_count,
        target_virtual_extruders
    );
    if (id_remap.empty()) {
        return;
    }

    for (VirtualExtruder& virtual_extruder : target_virtual_extruders) {
        const std::map<unsigned int, unsigned int>::const_iterator it =
            id_remap.find(virtual_extruder.id);
        if (it != id_remap.end()) {
            virtual_extruder.id = it->second;
        }
    }

    std::map<TriangleStateType, TriangleStateType> state_remap;
    for (const auto& [old_virtual_id, new_virtual_id] : id_remap) {
        state_remap[static_cast<TriangleStateType>(old_virtual_id)] =
            static_cast<TriangleStateType>(new_virtual_id);
    }

    for (ModelObject* obj : objects) {
        if (obj == nullptr) {
            continue;
        }

        for (ModelVolume* vol : obj->volumes) {
            if (vol == nullptr || !vol->is_mm_painted()) {
                continue;
            }

            const std::vector<bool>& used = vol->mm_segmentation_facets.get_data().used_states;
            bool needs_remap              = false;
            for (const TriangleStateType& old_state : state_remap | std::views::keys) {
                if (static_cast<size_t>(old_state) < used.size()
                    && used[static_cast<size_t>(old_state)])
                {
                    needs_remap = true;
                    break;
                }
            }

            if (!needs_remap) {
                continue;
            }

            TriangleSelector sel(vol->mesh());
            sel.deserialize(vol->mm_segmentation_facets.get_data(), false);
            sel.remap_states(state_remap);
            vol->mm_segmentation_facets.set_data(sel.serialize());
        }
    }

    SPDLOG_INFO(
        "VirtualExtruder: remapped {} virtual extruder IDs (source_physical={} "
        "target_physical={})",
        id_remap.size(),
        source_physical_count,
        target_physical_count
    );
}

} // namespace Slic3r::Biz::Algorithms::VirtualExtruder
