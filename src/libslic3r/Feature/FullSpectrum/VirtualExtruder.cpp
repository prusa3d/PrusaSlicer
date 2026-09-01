#include "VirtualExtruder.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>

#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

#include <prusa_fdm_mixer.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Surface.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>

using namespace Slic3r;

namespace Slic3r::FullSpectrum {

constexpr int FULL_SPECTRUM_CONFIG_VERSION = 1;

constexpr int BLEND_WEIGHT_RESOLUTION     = 64;
constexpr double BLEND_QUANTISE_MAX_ERROR = 0.03;

constexpr size_t GRADIENT_MIN_LAYERS_PER_BAND = 10;
constexpr size_t GRADIENT_MAX_BANDS           = 11;
constexpr double GRADIENT_RATIO_STEP          = 0.10;

bool is_virtual_extruder(unsigned int extruder_id_1based, const VirtualExtruders& virtual_extruders)
{
    for (const VirtualExtruder &virtual_extruder : virtual_extruders) {
        if (virtual_extruder.id == extruder_id_1based) {
            return true;
        }
    }

    return false;
}

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
static std::vector<int> quantise_component_counts(const VirtualExtruderComponents& components)
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
 * @return Cycle of 1-based physical extruder IDs, or empty if no components.
 */
static std::vector<unsigned int> build_canonical_cycle(const VirtualExtruderComponents& components)
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

/**
 * @brief Linearly interpolate gradient stop weights at position t.
 *
 * @param stops Gradient stops sorted by position.
 * @param t     Position in [0, 1] to sample.
 * @return Weight per stop, summing to 1.0.
 */
static std::vector<double> stop_weights_at_t(const VirtualExtruderGradientStops& stops, double t)
{
    std::vector<double> weights(stops.size(), 0.0);
    if (stops.empty()) {
        return weights;
    }

    if (t <= stops.front().position) {
        weights[0] = 1.0;
    } else if (t >= stops.back().position) {
        weights[stops.size() - 1] = 1.0;
    } else {
        size_t upper = 1;
        while (upper < stops.size() - 1 && stops[upper].position < t) {
            ++upper;
        }

        const size_t lower     = upper - 1;
        const double segment   = stops[upper].position - stops[lower].position;
        const double weight_up = (t - stops[lower].position) / segment;
        weights[lower]         = 1.0 - weight_up;
        weights[upper]         = weight_up;
    }

    return weights;
}

/**
 * @brief Round weights to multiples of step and re-normalize.
 *
 * @param[in,out] weights Weight vector to quantize in place.
 * @param         step    Quantization step (e.g. GRADIENT_RATIO_STEP = 0.10).
 */
static void quantize_weights_to_step(std::vector<double>& weights, double step)
{
    if (step <= 0.0) {
        return;
    }

    bool any_nonzero = false;
    double sum       = 0.0;
    for (double& w : weights) {
        w = std::round(w / step) * step;
        if (w > 0.0) {
            any_nonzero = true;
        }

        sum += w;
    }

    if (!any_nonzero) {
        return;
    }

    if (sum > 0.0) {
        for (double& w : weights) {
            w /= sum;
        }
    }
}

std::vector<unsigned int> VirtualExtruder::build_sequence() const
{
    assert(!this->gradient.has_value());

    if (this->gradient.has_value()) {
        const std::vector<VirtualExtruderGradientStop>& stops = this->gradient->stops;
        if (stops.size() < 2) {
            return {};
        }

        return {stops.front().extruder_id, stops.back().extruder_id};
    }

    VirtualExtruderComponents active;
    active.reserve(components.size());
    for (const VirtualExtruderComponent& component : components) {
        if (component.ratio > 0.0) {
            active.push_back(component);
        }
    }

    return build_canonical_cycle(active);
}

std::vector<unsigned int> VirtualExtruder::resolve_all_layers(
    const std::vector<double>& print_z_per_layer
) const
{
    const size_t num_layers = print_z_per_layer.size();

    if (this->gradient.has_value()) {
        if (!gradient->z_min.has_value() || !gradient->z_max.has_value()) {
            return std::vector<unsigned int>(num_layers, 0u);
        }

        return resolve_gradient_with_ranges(
            print_z_per_layer,
            {{*gradient->z_min, *gradient->z_max}}
        );
    }

    if (components.empty()) {
        return std::vector<unsigned int>(num_layers, 1u);
    }

    if (components.size() == 1) {
        return std::vector<unsigned int>(num_layers, components[0].extruder_id);
    }

    const std::vector<unsigned int> canonical_sequence = this->build_sequence();
    if (canonical_sequence.empty())
        return std::vector<unsigned int>(num_layers, components[0].extruder_id);

    std::vector<unsigned int> resolved_per_layer(num_layers);
    for (size_t layer_index = 0; layer_index < num_layers; ++layer_index) {
        resolved_per_layer[layer_index] =
            canonical_sequence[layer_index % canonical_sequence.size()];
    }

    return resolved_per_layer;
}

std::vector<unsigned int> VirtualExtruder::resolve_gradient_with_ranges(
    const std::vector<double>& print_z_per_layer,
    const std::vector<std::pair<double, double>>& z_ranges
) const
{
    const size_t num_layers = print_z_per_layer.size();
    std::vector<unsigned int> result(num_layers, 0u);

    if (!gradient.has_value() || gradient->stops.size() < 2) {
        return result;
    }

    const VirtualExtruderGradientStops& stops = gradient->stops;

    for (const auto& [range_min, range_max] : z_ranges) {
        const double z_span = range_max - range_min;
        if (!(z_span > 0.)) {
            continue;
        }

        std::vector<size_t> range_layer_indices;
        range_layer_indices.reserve(num_layers);
        for (size_t i = 0; i < num_layers; ++i) {
            if (print_z_per_layer[i] >= range_min && print_z_per_layer[i] <= range_max) {
                range_layer_indices.push_back(i);
            }
        }

        if (range_layer_indices.empty()) {
            continue;
        }

        const size_t range_layer_count = range_layer_indices.size();
        const size_t band_count        = std::clamp<size_t>(
            range_layer_count / GRADIENT_MIN_LAYERS_PER_BAND,
            2,
            GRADIENT_MAX_BANDS
        );

        std::vector<std::vector<unsigned int>> band_cycles(band_count);
        for (size_t b = 0; b < band_count; ++b) {
            const double midpoint_t          = (double(b) + 0.5) / double(band_count);
            std::vector<double> band_weights = stop_weights_at_t(stops, midpoint_t);
            quantize_weights_to_step(band_weights, GRADIENT_RATIO_STEP);

            std::vector<VirtualExtruderComponent> band_components;
            band_components.reserve(stops.size());
            for (size_t i = 0; i < stops.size(); ++i) {
                if (band_weights[i] > 0.) {
                    band_components.push_back({stops[i].extruder_id, band_weights[i]});
                }
            }

            band_cycles[b] = build_canonical_cycle(band_components);
            if (band_cycles[b].empty()) {
                const size_t nearest = (midpoint_t <= 0.5) ? 0 : (stops.size() - 1);
                band_cycles[b]       = {stops[nearest].extruder_id};
            }
        }

        std::vector<size_t> band_first_offset(band_count, range_layer_count);
        for (size_t offset = 0; offset < range_layer_count; ++offset) {
            const size_t li = range_layer_indices[offset];
            const double t  = std::clamp((print_z_per_layer[li] - range_min) / z_span, 0.0, 1.0);
            const size_t b  = std::min<size_t>(size_t(t * double(band_count)), band_count - 1);
            if (band_first_offset[b] == range_layer_count) {
                band_first_offset[b] = offset;
            }

            const size_t cycle_idx = (offset - band_first_offset[b]) % band_cycles[b].size();
            result[li]             = band_cycles[b][cycle_idx];
        }
    }

    return result;
}

std::vector<std::pair<double, double>> detect_gradient_ranges(
    const std::vector<double>& print_z_per_layer,
    const std::vector<bool>& layer_has_content
)
{
    assert(print_z_per_layer.size() == layer_has_content.size());
    std::vector<std::pair<double, double>> ranges;
    const size_t n = layer_has_content.size();
    size_t i       = 0;
    while (i < n) {
        if (!layer_has_content[i]) {
            ++i;
            continue;
        }
        const size_t first = i;
        while (i < n && layer_has_content[i]) {
            ++i;
        }

        const size_t last = i - 1;
        ranges.emplace_back(print_z_per_layer[first], print_z_per_layer[last]);
    }

    return ranges;
}

std::vector<VirtualExtruder> normalize_virtual_extruders(
    const VirtualExtruders& raw_virtual_extruders
)
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
                BOOST_LOG_TRIVIAL(error)
                    << "VirtualExtruder id="
                    << raw_virtual_extruder.id
                    << " dropped: gradient z_max ("
                    << *raw_gradient.z_max
                    << ") must be strictly greater than z_min ("
                    << *raw_gradient.z_min
                    << ")";
                continue;
            }

            VirtualExtruderGradientStops sorted_stops = raw_gradient.stops;
            std::sort(
                sorted_stops.begin(),
                sorted_stops.end(),
                [](const VirtualExtruderGradientStop& a, const VirtualExtruderGradientStop& b)
                { return a.position < b.position; }
            );

            VirtualExtruderGradientStops deduped_stops;
            deduped_stops.reserve(sorted_stops.size());
            for (const VirtualExtruderGradientStop& stop : sorted_stops) {
                if (!(stop.position >= 0.0 && stop.position <= 1.0)) {
                    BOOST_LOG_TRIVIAL(error)
                        << "VirtualExtruder id="
                        << raw_virtual_extruder.id
                        << " gradient drops stop with position "
                        << stop.position
                        << " (must be in [0, 1])";
                    continue;
                }

                if (!deduped_stops.empty()
                    && std::abs(deduped_stops.back().position - stop.position) < 1e-9)
                {
                    BOOST_LOG_TRIVIAL(error)
                        << "VirtualExtruder id="
                        << raw_virtual_extruder.id
                        << " gradient drops duplicate-position stop at "
                        << stop.position;
                    continue;
                }

                deduped_stops.push_back(stop);
            }

            if (deduped_stops.size() < 2) {
                BOOST_LOG_TRIVIAL(error)
                    << "VirtualExtruder id="
                    << raw_virtual_extruder.id
                    << " dropped: gradient mode requires >= 2 distinct-position stops, got "
                    << deduped_stops.size();
                continue;
            }

            std::set<unsigned int> distinct_extruders;
            for (const VirtualExtruderGradientStop& stop : deduped_stops) {
                distinct_extruders.insert(stop.extruder_id);
            }

            if (distinct_extruders.size() < 2) {
                BOOST_LOG_TRIVIAL(error)
                    << "VirtualExtruder id="
                    << raw_virtual_extruder.id
                    << " dropped: gradient stops reference fewer than 2 distinct physical extruders";
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
                BOOST_LOG_TRIVIAL(warning)
                    << "VirtualExtruder id="
                    << raw_virtual_extruder.id
                    << " drops component with negative ratio "
                    << component.ratio;
                continue;
            }

            normalized_virtual_extruder.components.push_back(component);
            ratio_sum += component.ratio;
        }

        if (normalized_virtual_extruder.components.size() < 2) {
            BOOST_LOG_TRIVIAL(error)
                << "VirtualExtruder id="
                << raw_virtual_extruder.id
                << " dropped: fewer than 2 valid components remain";
            continue;
        }

        if (normalized_virtual_extruder.components.size() > MAX_BLEND_COMPONENTS) {
            BOOST_LOG_TRIVIAL(error)
                << "VirtualExtruder id="
                << raw_virtual_extruder.id
                << " dropped: blend recipe has "
                << normalized_virtual_extruder.components.size()
                << " components (max "
                << MAX_BLEND_COMPONENTS
                << ")";
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

std::vector<VirtualExtruder> filter_virtual_extruders_for_physical_count(
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
                    BOOST_LOG_TRIVIAL(warning)
                        << "VirtualExtruder id="
                        << virtual_extruder.id
                        << " dropped: gradient stop references extruder "
                        << stop.extruder_id
                        << " (only "
                        << num_physical
                        << " physical extruders available)";
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
                BOOST_LOG_TRIVIAL(warning)
                    << "VirtualExtruder id="
                    << virtual_extruder.id
                    << " dropped: component references extruder "
                    << component.extruder_id
                    << " (only "
                    << num_physical
                    << " physical extruders available)";
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
                // Gradient mode: stops carry the physical references; components is unused.
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

    sort_remove_duplicates(result);
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
            && component.ratio > 0.0)
        {
            parts.push_back({physical_colors[component.extruder_id - 1], component.ratio});
        }
    }

    if (parts.empty()) {
        return {};
    }

    return prusa_fdm_mixer::mix(parts);
}

std::string VirtualExtruder::effective_color(
    const std::vector<std::string>& physical_extruders_colors_0based
) const
{
    if (this->color.has_value()) {
        return *this->color;
    }

    if (this->gradient.has_value() && this->gradient->stops.size() >= 2) {
        const VirtualExtruderGradientStops& stops = this->gradient->stops;
        std::vector<VirtualExtruderComponent> weighted_blend;
        weighted_blend.reserve(stops.size());
        for (size_t i = 0; i < stops.size(); ++i) {
            const double lo =
                (i == 0) ? 0.0 : (stops[i - 1].position + stops[i].position) * 0.5;
            const double hi =
                (i == stops.size() - 1) ? 1.0 : (stops[i].position + stops[i + 1].position) * 0.5;
            weighted_blend.push_back({stops[i].extruder_id, std::max(0.0, hi - lo)});
        }

        return blend_virtual_extruder_color(weighted_blend, physical_extruders_colors_0based);
    }

    return blend_virtual_extruder_color(this->components, physical_extruders_colors_0based);
}

std::string serialize_gradient_color_string(const VirtualExtruderGradientStops &stops,
    const std::vector<std::string>& physical_colors
)
{
    if (stops.size() < 2) {
        return {};
    }

    auto color_for_stop = [&](const VirtualExtruderGradientStop& stop) -> std::string
    {
        if (stop.extruder_id >= 1 && stop.extruder_id <= physical_colors.size()) {
            const std::string& c = physical_colors[stop.extruder_id - 1];
            if (c.size() == 7 && c.front() == '#') {
                return c;
            }
        }

        return "#808080";
    };

    const bool elide_positions = stops.size() == 2
        && std::abs(stops.front().position - 0.) < EPSILON
        && std::abs(stops.back().position - 1.) < EPSILON;

    std::ostringstream out;
    out << "g:";
    for (size_t i = 0; i < stops.size(); ++i) {
        if (i > 0) {
            out << ':';
        }

        out << color_for_stop(stops[i]);
        if (!elide_positions) {
            out << ',' << stops[i].position;
        }
    }

    return out.str();
}

std::string serialize_virtual_extruders_to_json(
    const std::vector<std::string>& physical_extruders_colors,
    const VirtualExtruders& virtual_extruders
)
{
    nlohmann::json root;
    root["version"] = FULL_SPECTRUM_CONFIG_VERSION;

    nlohmann::json physical_extruders_array = nlohmann::json::array();
    for (size_t physical_extruder_idx = 0; physical_extruder_idx < physical_extruders_colors.size();
         ++physical_extruder_idx)
    {
        nlohmann::json entry;
        entry["id"]    = static_cast<int>(physical_extruder_idx + 1);
        entry["color"] = physical_extruders_colors[physical_extruder_idx];
        physical_extruders_array.push_back(entry);
    }

    root["physical_extruders"] = physical_extruders_array;

    nlohmann::json virtual_extruders_array = nlohmann::json::array();
    for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
        nlohmann::json entry;
        entry["id"] = static_cast<int>(virtual_extruder.id);

        if (virtual_extruder.gradient.has_value()) {
            entry["kind"]                    = "gradient";
            const std::string gradient_color = serialize_gradient_color_string(
                virtual_extruder.gradient->stops,
                physical_extruders_colors
            );
            if (!gradient_color.empty()) {
                entry["color"] = gradient_color;
            }

            if (virtual_extruder.gradient->z_min.has_value()) {
                entry["minz"] = *virtual_extruder.gradient->z_min;
            }

            if (virtual_extruder.gradient->z_max.has_value()) {
                entry["maxz"] = *virtual_extruder.gradient->z_max;
            }

            nlohmann::json components_array = nlohmann::json::array();
            for (const VirtualExtruderGradientStop& stop : virtual_extruder.gradient->stops) {
                nlohmann::json component_entry;
                component_entry["extruder"] = static_cast<int>(stop.extruder_id);
                component_entry["position"] = stop.position;
                components_array.push_back(component_entry);
            }

            entry["components"] = components_array;
        } else {
            entry["kind"]           = "fullspectrum";
            const std::string color = virtual_extruder.effective_color(physical_extruders_colors);
            if (!color.empty()) {
                entry["color"] = color;
            }

            nlohmann::json components_array = nlohmann::json::array();
            for (const VirtualExtruderComponent& component : virtual_extruder.components) {
                nlohmann::json component_entry;
                component_entry["extruder"] = static_cast<int>(component.extruder_id);
                component_entry["ratio"]    = component.ratio;
                components_array.push_back(component_entry);
            }

            entry["components"] = components_array;
        }

        virtual_extruders_array.push_back(entry);
    }

    root["virtual_extruders"] = virtual_extruders_array;

    return root.dump(4);
}

FullSpectrumConfig deserialize_virtual_extruders_from_json(
    const std::string& json_content
)
{
    constexpr unsigned int max_extruder_id =
        static_cast<unsigned int>(TRIANGLE_STATE_TYPE_COUNT) - 1;

    FullSpectrumConfig result;

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_content);
    } catch (const nlohmann::json::parse_error& parse_error) {
        BOOST_LOG_TRIVIAL(error)
            << "Prusa_Slicer_full_spectrum.json: JSON parse error: "
            << parse_error.what();
        return result;
    }

    const int version = root.value("version", 0);
    if (version == 0) {
        BOOST_LOG_TRIVIAL(error) << "Prusa_Slicer_full_spectrum.json: missing version field";
        return result;
    }

    if (version > FULL_SPECTRUM_CONFIG_VERSION) {
        BOOST_LOG_TRIVIAL(error)
            << "Prusa_Slicer_full_spectrum.json: unsupported version "
            << version
            << " (max supported: "
            << FULL_SPECTRUM_CONFIG_VERSION
            << ")";
        return result;
    }

    if (root.contains("physical_extruders") && root["physical_extruders"].is_array()) {
        const nlohmann::json& phys_array = root["physical_extruders"];
        result.source_physical_count     = static_cast<unsigned int>(phys_array.size());
        result.physical_colors.reserve(phys_array.size());
        for (const nlohmann::json& phys_entry : phys_array) {
            result.physical_colors.push_back(phys_entry.value("color", std::string("#808080")));
        }
    }

    if (!root.contains("virtual_extruders") || !root["virtual_extruders"].is_array()) {
        return result;
    }

    const nlohmann::json& virtual_extruders_node = root["virtual_extruders"];

    for (const nlohmann::json& entry : virtual_extruders_node) {
        const int id = entry.value("id", -1);
        if (id <= 0) {
            BOOST_LOG_TRIVIAL(error)
                << "Prusa_Slicer_full_spectrum.json: entry missing or non-positive id, skipping";
            continue;
        }

        const std::string kind_raw = entry.value("kind", std::string("fullspectrum"));
        const bool has_components  = entry.contains("components") && entry["components"].is_array();
        bool is_gradient           = (kind_raw == "gradient");
        if (!is_gradient && has_components) {
            for (const nlohmann::json& component : entry["components"]) {
                if (component.contains("position")) {
                    BOOST_LOG_TRIVIAL(warning)
                        << "Prusa_Slicer_full_spectrum.json: entry id="
                        << id
                        << " has 'position' field without kind=\"gradient\"; treating as gradient";
                    is_gradient = true;
                    break;
                }
            }
        }

        VirtualExtruder virtual_extruder{static_cast<unsigned int>(id), std::nullopt};

        if (is_gradient) {
            std::optional<double> z_min;
            std::optional<double> z_max;
            if (entry.contains("minz") && entry["minz"].is_number()) {
                z_min = entry["minz"].get<double>();
            }

            if (entry.contains("maxz") && entry["maxz"].is_number()) {
                z_max = entry["maxz"].get<double>();
            }

            if (z_min.has_value() && z_max.has_value()) {
                if (!std::isfinite(*z_min) || !std::isfinite(*z_max) || !(*z_max > *z_min)) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Prusa_Slicer_full_spectrum.json: entry id="
                        << id
                        << " gradient has invalid minz="
                        << *z_min
                        << " maxz="
                        << *z_max
                        << " (maxz must be strictly greater than minz), skipping entry";
                    continue;
                }
            }

            if (!has_components) {
                BOOST_LOG_TRIVIAL(error)
                    << "Prusa_Slicer_full_spectrum.json: entry id="
                    << id
                    << " gradient missing components, skipping";
                continue;
            }

            const nlohmann::json& components_array = entry["components"];
            std::vector<VirtualExtruderGradientStop> stops;
            bool entry_is_valid = true;
            for (const nlohmann::json& component_entry : components_array) {
                if (component_entry.contains("ratio")) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Prusa_Slicer_full_spectrum.json: entry id="
                        << id
                        << " gradient component has 'ratio' (expected 'position'), skipping entry";
                    entry_is_valid = false;
                    break;
                }

                const int extruder = component_entry.value("extruder", -1);
                const double position =
                    component_entry.value("position", std::numeric_limits<double>::quiet_NaN());
                if (extruder < 1 || extruder > static_cast<int>(max_extruder_id)) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Prusa_Slicer_full_spectrum.json: entry id="
                        << id
                        << " gradient stop has invalid extruder "
                        << extruder
                        << ", skipping entry";
                    entry_is_valid = false;
                    break;
                }

                if (!std::isfinite(position) || position < 0.0 || position > 1.0) {
                    BOOST_LOG_TRIVIAL(error)
                        << "Prusa_Slicer_full_spectrum.json: entry id="
                        << id
                        << " gradient stop has invalid position "
                        << position
                        << " (expected in [0, 1]), skipping entry";
                    entry_is_valid = false;
                    break;
                }

                stops.push_back({static_cast<unsigned int>(extruder), position});
            }

            if (!entry_is_valid) {
                continue;
            }

            if (stops.size() < 2) {
                BOOST_LOG_TRIVIAL(error)
                    << "Prusa_Slicer_full_spectrum.json: entry id="
                    << id
                    << " gradient needs >= 2 stops, got "
                    << stops.size()
                    << ", skipping";
                continue;
            }

            virtual_extruder.gradient = VirtualExtruderGradient{z_min, z_max, std::move(stops)};
            result.virtual_extruders.push_back(std::move(virtual_extruder));
            continue;
        }

        if (!has_components) {
            BOOST_LOG_TRIVIAL(error)
                << "Prusa_Slicer_full_spectrum.json: entry id="
                << id
                << " missing components, skipping";
            continue;
        }

        if (entry.contains("color") && entry["color"].is_string()) {
            const std::string color_raw = entry["color"].get<std::string>();
            if (color_raw.size() >= 2 && color_raw.compare(0, 2, "g:") == 0) {
                BOOST_LOG_TRIVIAL(error)
                    << "Prusa_Slicer_full_spectrum.json: entry id="
                    << id
                    << " kind=fullspectrum has gradient color string, ignoring color";
            } else {
                virtual_extruder.color = color_raw;
            }
        }

        const nlohmann::json& components_array = entry["components"];
        bool entry_is_valid                    = true;
        for (const nlohmann::json& component_entry : components_array) {
            if (component_entry.contains("position")) {
                BOOST_LOG_TRIVIAL(error)
                    << "Prusa_Slicer_full_spectrum.json: entry id="
                    << id
                    << " kind=fullspectrum component has 'position' (expected 'ratio'), skipping entry";
                entry_is_valid = false;
                break;
            }
            const int extruder = component_entry.value("extruder", -1);
            const double ratio = component_entry.value("ratio", 0.);
            if (extruder < 1 || extruder > static_cast<int>(max_extruder_id)) {
                BOOST_LOG_TRIVIAL(error)
                    << "Prusa_Slicer_full_spectrum.json: entry id="
                    << id
                    << " has component with invalid extruder "
                    << extruder
                    << ", skipping entry";
                entry_is_valid = false;
                break;
            }

            virtual_extruder.components.push_back({static_cast<unsigned int>(extruder), ratio});
        }

        if (!entry_is_valid) {
            continue;
        }

        if (virtual_extruder.components.empty()) {
            BOOST_LOG_TRIVIAL(error)
                << "Prusa_Slicer_full_spectrum.json: entry id="
                << id
                << " has no valid components, skipping";
            continue;
        }

        result.virtual_extruders.push_back(std::move(virtual_extruder));
    }

    return result;
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
            BOOST_LOG_TRIVIAL(error)
                << "FullSpectrum: remap would push virtual extruder id="
                << ve.id
                << " to "
                << new_id
                << " which exceeds max "
                << max_id
                << ", aborting remap";
            return {};
        }

        remap[ve.id] = new_id;
    }

    return remap;
}

void remap_full_spectrum_on_import(
    Model& loaded_model,
    VirtualExtruders& target_virtual_extruders,
    unsigned int target_physical_count,
    const FullSpectrumConfig& fs_config
)
{
    if (fs_config.source_physical_count == 0 || target_virtual_extruders.empty()) {
        return;
    }

    const std::map<unsigned int, unsigned int> id_remap = compute_virtual_id_remap(
        fs_config.source_physical_count,
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
    for (const std::pair<const unsigned int, unsigned int>& entry : id_remap) {
        state_remap[static_cast<TriangleStateType>(entry.first)] =
            static_cast<TriangleStateType>(entry.second);
    }

    for (ModelObject* obj : loaded_model.objects) {
        if (obj == nullptr) {
            continue;
        }

        for (ModelVolume* vol : obj->volumes) {
            if (vol == nullptr || !vol->is_mm_painted()) {
                continue;
            }

            const std::vector<bool>& used = vol->mm_segmentation_facets.get_data().used_states;
            bool needs_remap              = false;
            for (const std::pair<const TriangleStateType, TriangleStateType>& entry : state_remap) {
                if (static_cast<size_t>(entry.first) < used.size()
                    && used[static_cast<size_t>(entry.first)])
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
            vol->mm_segmentation_facets.set(sel);
        }
    }

    BOOST_LOG_TRIVIAL(info)
        << "FullSpectrum: remapped "
        << id_remap.size()
        << " virtual extruder IDs (source_physical="
        << fs_config.source_physical_count
        << " target_physical="
        << target_physical_count
        << ")";
}

std::optional<unsigned int> source_virtual_extruder_in_region_config(
    const PrintRegionConfig& config,
    const VirtualExtruders& virtual_extruders
)
{
    if (virtual_extruders.empty()) {
        return std::nullopt;
    }

    const unsigned int id = static_cast<unsigned int>(config.perimeter_extruder.value);
    for (const VirtualExtruder& ve : virtual_extruders) {
        if (ve.id == id) {
            return id;
        }
    }

    return std::nullopt;
}

void remap_virtual_region_slices_to_physical(
    PrintObject& print_object,
    unsigned int num_physical,
    const VirtualExtruders& virtual_extruders
)
{
    const PrintObjectRegions* shared_regions = print_object.shared_regions();
    if (shared_regions == nullptr) {
        return;
    }

    // Maps a virtual-extruder region to the painted physical-extruder regions
    // that were split from it.
    struct VirtualRegionMapping
    {
        // PrintRegion index of the original virtual-extruder region (-1 = unset).
        int virtual_region_id{-1};
        // 1-based ID of the virtual extruder assigned to this region.
        unsigned int virtual_extruder_id{0};
        // Physical extruder ID (1-based) to PrintRegion index of the painted region.
        std::map<unsigned int, int> physical_targets;
    };

    std::vector<VirtualRegionMapping> mappings;

    for (const PrintObjectRegions::LayerRangeRegions& layer_range : shared_regions->layer_ranges) {
        const int num_volume_regions = int(layer_range.volume_regions.size());
        for (int volume_region_id = 0; volume_region_id < num_volume_regions; ++volume_region_id) {
            const PrintObjectRegions::VolumeRegion& volume_region =
                layer_range.volume_regions[volume_region_id];
            if (volume_region.region == nullptr) {
                continue;
            }

            const std::optional<unsigned int> source_virtual =
                volume_region.region->source_virtual_extruder_id();
            if (!source_virtual.has_value()) {
                continue;
            }

            VirtualRegionMapping mapping;
            mapping.virtual_region_id   = volume_region.region->print_object_region_id();
            mapping.virtual_extruder_id = *source_virtual;

            for (const PrintObjectRegions::PaintedRegion& painted_region :
                 layer_range.painted_regions)
            {
                if (painted_region.parent == volume_region_id && painted_region.region != nullptr) {
                    assert(
                        painted_region.extruder_id >= 1
                        && painted_region.extruder_id <= num_physical
                    );
                    mapping.physical_targets[painted_region.extruder_id] =
                        painted_region.region->print_object_region_id();
                }
            }

            if (!mapping.physical_targets.empty()) {
                mappings.push_back(std::move(mapping));
            }
        }
    }

    if (mappings.empty()) {
        return;
    }

    const size_t num_layers = print_object.layer_count();

    std::vector<double> print_z_per_layer(num_layers);
    for (size_t layer_index = 0; layer_index < num_layers; ++layer_index) {
        print_z_per_layer[layer_index] = print_object.get_layer(int(layer_index))->print_z;
    }

    std::map<unsigned int, std::vector<unsigned int>> sequences_per_virtual_extruder;
    for (const VirtualRegionMapping& mapping : mappings) {
        if (sequences_per_virtual_extruder.count(mapping.virtual_extruder_id) != 0) {
            continue;
        }

        for (const VirtualExtruder& candidate : virtual_extruders) {
            if (candidate.id == mapping.virtual_extruder_id) {
                if (candidate.type() == VirtualExtruder::Type::Gradient
                    && candidate.gradient.has_value()
                    && (!candidate.gradient->z_min.has_value()
                        || !candidate.gradient->z_max.has_value())
                    && !print_z_per_layer.empty())
                {
                    std::vector<bool> layer_mask(num_layers, false);
                    for (const VirtualRegionMapping& m : mappings) {
                        if (m.virtual_extruder_id != mapping.virtual_extruder_id) {
                            continue;
                        }

                        for (size_t li = 0; li < num_layers; ++li) {
                            Layer* layer    = print_object.get_layer(int(li));
                            LayerRegion* lr = layer->get_region(m.virtual_region_id);
                            if (lr && !lr->slices().empty())
                                layer_mask[li] = true;
                        }
                    }

                    const auto ranges = detect_gradient_ranges(print_z_per_layer, layer_mask);
                    sequences_per_virtual_extruder[mapping.virtual_extruder_id] =
                        candidate.resolve_gradient_with_ranges(print_z_per_layer, ranges);
                } else {
                    sequences_per_virtual_extruder[mapping.virtual_extruder_id] =
                        candidate.resolve_all_layers(print_z_per_layer);
                }
                break;
            }
        }
    }

    for (size_t layer_index = 0; layer_index < num_layers; ++layer_index) {
        Layer* layer = print_object.get_layer(int(layer_index));
        for (const VirtualRegionMapping& mapping : mappings) {
            LayerRegion* source_layer_region = layer->get_region(mapping.virtual_region_id);
            if (source_layer_region->slices().empty()) {
                continue;
            }

            const std::vector<unsigned int>& resolved_sequence =
                sequences_per_virtual_extruder.at(mapping.virtual_extruder_id);
            const unsigned int resolved_physical_extruder_id = resolved_sequence[layer_index];
            if (resolved_physical_extruder_id == 0) {
                continue;
            }

            const std::map<unsigned int, int>::const_iterator target_it =
                mapping.physical_targets.find(resolved_physical_extruder_id);
            if (target_it == mapping.physical_targets.end()) {
                assert(false);
                BOOST_LOG_TRIVIAL(error)
                    << "FullSpectrum remap: virtual extruder id="
                    << mapping.virtual_extruder_id
                    << " layer="
                    << layer_index
                    << " resolved to physical extruder "
                    << resolved_physical_extruder_id
                    << " but no matching PrintRegion exists — slices stay in virtual region";
                continue;
            }

            assert(target_it->second != mapping.virtual_region_id);

            LayerRegion* destination_layer_region = layer->get_region(target_it->second);
            if (destination_layer_region->slices().empty()) {
                destination_layer_region->m_slices.append(std::move(source_layer_region->m_slices));
            } else {
                ExPolygons merged_slices =
                    to_expolygons(destination_layer_region->slices().surfaces);
                append(merged_slices, to_expolygons(source_layer_region->slices().surfaces));
                merged_slices = closing_ex(merged_slices, scaled<float>(10. * EPSILON));
                destination_layer_region->m_slices.set(std::move(merged_slices), stInternal);
            }

            source_layer_region->m_slices.clear();
        }
    }
}

void remap_virtual_extruders_to_physical(
    std::vector<std::vector<ExPolygons>>& segmentation,
    const std::vector<double>& print_z_per_layer,
    const unsigned int num_physical_extruders,
    const VirtualExtruders& virtual_extruders
)
{
    const size_t num_layers = segmentation.size();
    assert(print_z_per_layer.size() == num_layers);

    std::map<unsigned int, std::vector<unsigned int>> sequences_per_virtual_extruder;
    for (const VirtualExtruder& virtual_extruder : virtual_extruders) {
        const bool is_auto_gradient = virtual_extruder.type() == VirtualExtruder::Type::Gradient
            && virtual_extruder.gradient.has_value()
            && (!virtual_extruder.gradient->z_min.has_value()
                || !virtual_extruder.gradient->z_max.has_value());
        if (is_auto_gradient && !print_z_per_layer.empty()) {
            std::vector<bool> layer_mask(num_layers, false);
            const size_t slot = virtual_extruder.id - 1;
            for (size_t li = 0; li < num_layers; ++li) {
                if (slot < segmentation[li].size() && !segmentation[li][slot].empty()) {
                    layer_mask[li] = true;
                }
            }

            const auto ranges = detect_gradient_ranges(print_z_per_layer, layer_mask);
            sequences_per_virtual_extruder[virtual_extruder.id] =
                virtual_extruder.resolve_gradient_with_ranges(print_z_per_layer, ranges);
        } else {
            sequences_per_virtual_extruder[virtual_extruder.id] =
                virtual_extruder.resolve_all_layers(print_z_per_layer);
        }
    }

    for (size_t layer_index = 0; layer_index < num_layers; ++layer_index) {
        std::vector<ExPolygons>& segmentation_for_layer = segmentation[layer_index];
        for (size_t slot_index = num_physical_extruders; slot_index < segmentation_for_layer.size();
             ++slot_index)
        {
            if (segmentation_for_layer[slot_index].empty()) {
                continue;
            }

            const unsigned int extruder_id_1based = static_cast<unsigned int>(slot_index + 1);

            unsigned int physical_extruder_id = extruder_id_1based;
            const std::map<unsigned int, std::vector<unsigned int>>::const_iterator sequence_it =
                sequences_per_virtual_extruder.find(extruder_id_1based);
            if (sequence_it != sequences_per_virtual_extruder.end()) {
                if (const unsigned int resolved = sequence_it->second[layer_index]; resolved > 0) {
                    physical_extruder_id = resolved;
                }
            }

            if (physical_extruder_id < 1 || physical_extruder_id > num_physical_extruders) {
                continue;
            }

            append(
                segmentation_for_layer[physical_extruder_id - 1],
                std::move(segmentation_for_layer[slot_index])
            );
            segmentation_for_layer[slot_index].clear();
        }
    }
}

} // namespace Slic3r::FullSpectrum
