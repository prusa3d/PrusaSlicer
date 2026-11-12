#include "VirtualExtruder.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"
#include "Slic3r/Log.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <numeric>
#include <set>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/LayerRegion.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Surface.hpp"

using namespace Slic3r;
using namespace Slic3r::Biz;

namespace Slic3r::Feature::VirtualExtruder {

using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruderComponent;
using Slic3r::Domain::VirtualExtruderComponents;
using Slic3r::Domain::VirtualExtruderGradientStop;
using Slic3r::Domain::VirtualExtruderGradientStops;
using Slic3r::Domain::VirtualExtruders;

namespace {

constexpr size_t GRADIENT_MIN_LAYERS_PER_BAND = 10;
constexpr size_t GRADIENT_MAX_BANDS           = 11;
constexpr double GRADIENT_RATIO_STEP          = 0.10;

/**
 * @brief Linearly interpolate gradient stop weights at position t.
 *
 * @param stops Gradient stops sorted by position.
 * @param t     Position in [0, 1] to sample.
 * @return Weight per stop, summing to 1.0.
 */
std::vector<double> stop_weights_at_t(const VirtualExtruderGradientStops& stops, double t)
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
void quantize_weights_to_step(std::vector<double>& weights, double step)
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

} // namespace

std::vector<unsigned int> resolve_all_layers(
    const VirtualExtruder& virtual_extruder,
    const std::vector<double>& print_z_per_layer
)
{
    const size_t num_layers = print_z_per_layer.size();

    if (virtual_extruder.gradient.has_value()) {
        if (!virtual_extruder.gradient->z_min.has_value()
            || !virtual_extruder.gradient->z_max.has_value())
        {
            return std::vector<unsigned int>(num_layers, 0u);
        }

        return resolve_gradient_with_ranges(
            virtual_extruder,
            print_z_per_layer,
            {{*virtual_extruder.gradient->z_min, *virtual_extruder.gradient->z_max}}
        );
    }

    if (virtual_extruder.components.empty()) {
        return std::vector<unsigned int>(num_layers, 1u);
    }

    if (virtual_extruder.components.size() == 1) {
        return std::vector<unsigned int>(num_layers, virtual_extruder.components[0].extruder_id);
    }

    const std::vector<unsigned int> canonical_sequence =
        Algorithms::VirtualExtruder::build_sequence(virtual_extruder);
    if (canonical_sequence.empty()) {
        return std::vector<unsigned int>(num_layers, virtual_extruder.components[0].extruder_id);
    }

    std::vector<unsigned int> resolved_per_layer(num_layers);
    for (size_t layer_index = 0; layer_index < num_layers; ++layer_index) {
        resolved_per_layer[layer_index] =
            canonical_sequence[layer_index % canonical_sequence.size()];
    }

    return resolved_per_layer;
}

std::vector<unsigned int> resolve_gradient_with_ranges(
    const VirtualExtruder& virtual_extruder,
    const std::vector<double>& print_z_per_layer,
    const std::vector<std::pair<double, double>>& z_ranges
)
{
    const size_t num_layers = print_z_per_layer.size();
    std::vector<unsigned int> result(num_layers, 0u);

    if (!virtual_extruder.gradient.has_value() || virtual_extruder.gradient->stops.size() < 2) {
        return result;
    }

    const VirtualExtruderGradientStops& stops = virtual_extruder.gradient->stops;

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
        const size_t band_count        = std::clamp<
                   size_t>(range_layer_count / GRADIENT_MIN_LAYERS_PER_BAND, 2, GRADIENT_MAX_BANDS);

        std::vector<std::vector<unsigned int>> band_cycles(band_count);
        for (size_t b = 0; b < band_count; ++b) {
            const double midpoint_t =
                (static_cast<double>(b) + 0.5) / static_cast<double>(band_count);
            std::vector<double> band_weights = stop_weights_at_t(stops, midpoint_t);
            quantize_weights_to_step(band_weights, GRADIENT_RATIO_STEP);

            std::vector<VirtualExtruderComponent> band_components;
            band_components.reserve(stops.size());
            for (size_t i = 0; i < stops.size(); ++i) {
                if (band_weights[i] > 0.) {
                    band_components.push_back({stops[i].extruder_id, band_weights[i]});
                }
            }

            band_cycles[b] = Algorithms::VirtualExtruder::build_canonical_cycle(band_components);
            if (band_cycles[b].empty()) {
                const size_t nearest = (midpoint_t <= 0.5) ? 0 : (stops.size() - 1);
                band_cycles[b]       = {stops[nearest].extruder_id};
            }
        }

        std::vector<size_t> band_first_offset(band_count, range_layer_count);
        for (size_t offset = 0; offset < range_layer_count; ++offset) {
            const size_t li = range_layer_indices[offset];
            const double t  = std::clamp((print_z_per_layer[li] - range_min) / z_span, 0.0, 1.0);
            const size_t b  = std::min<
                 size_t>(static_cast<size_t>(t * static_cast<double>(band_count)), band_count - 1);
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
        const int num_volume_regions = static_cast<int>(layer_range.volume_regions.size());
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
        print_z_per_layer[layer_index] =
            print_object.get_layer(static_cast<int>(layer_index))->print_z;
    }

    std::map<unsigned int, std::vector<unsigned int>> sequences_per_virtual_extruder;
    for (const VirtualRegionMapping& mapping : mappings) {
        if (sequences_per_virtual_extruder.contains(mapping.virtual_extruder_id)) {
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
                            Layer* layer    = print_object.get_layer(static_cast<int>(li));
                            LayerRegion* lr = layer->get_region(m.virtual_region_id);
                            if (lr && !lr->slices().empty())
                                layer_mask[li] = true;
                        }
                    }

                    const auto ranges = detect_gradient_ranges(print_z_per_layer, layer_mask);
                    sequences_per_virtual_extruder[mapping.virtual_extruder_id] =
                        resolve_gradient_with_ranges(candidate, print_z_per_layer, ranges);
                } else {
                    sequences_per_virtual_extruder[mapping.virtual_extruder_id] =
                        resolve_all_layers(candidate, print_z_per_layer);
                }
                break;
            }
        }
    }

    for (size_t layer_index = 0; layer_index < num_layers; ++layer_index) {
        Layer* layer = print_object.get_layer(static_cast<int>(layer_index));
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
                SPDLOG_ERROR(
                    "VirtualExtruder remap: virtual extruder id={} layer={} resolved to physical "
                    "extruder {} but no matching PrintRegion exists — slices stay in virtual region",
                    mapping.virtual_extruder_id,
                    layer_index,
                    resolved_physical_extruder_id
                );
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
                resolve_gradient_with_ranges(virtual_extruder, print_z_per_layer, ranges);
        } else {
            sequences_per_virtual_extruder[virtual_extruder.id] =
                resolve_all_layers(virtual_extruder, print_z_per_layer);
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

            Slic3r::append(
                segmentation_for_layer[physical_extruder_id - 1],
                std::move(segmentation_for_layer[slot_index])
            );
            segmentation_for_layer[slot_index].clear();
        }
    }
}

} // namespace Slic3r::Feature::VirtualExtruder
