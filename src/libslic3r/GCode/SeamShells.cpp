#include "libslic3r/GCode/SeamShells.hpp"

#include <algorithm>
#include <optional>
#include <utility>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"

namespace Slic3r::Seams::Shells::Impl {

Shells<> map_to_shells(
    Perimeters::LayerPerimeters &&layers, const Geometry::Mapping &mapping, const std::size_t shell_count
) {
    Shells<> result(shell_count);
    for (std::size_t layer_index{0}; layer_index < layers.size(); ++layer_index) {
        Perimeters::BoundedPerimeters &perimeters{layers[layer_index]};
        for (std::size_t perimeter_index{0}; perimeter_index < perimeters.size();
             perimeter_index++) {
            Perimeters::Perimeter &perimeter{perimeters[perimeter_index].perimeter};
            result[mapping[layer_index][perimeter_index]].push_back(
                Slice<>{std::move(perimeter), layer_index}
            );
        }
    }
    return result;
}
} // namespace Slic3r::Seams::Shells::Impl

namespace Slic3r::Seams::Shells {
Shells<> create_shells(
    Perimeters::LayerPerimeters &&perimeters, const double max_distance
) {
    using Perimeters::BoundedPerimeters;
    using Perimeters::BoundedPerimeter;

    std::vector<std::size_t> layer_sizes;
    layer_sizes.reserve(perimeters.size());
    for (const BoundedPerimeters &layer : perimeters) {
        layer_sizes.push_back(layer.size());
    }

    const auto &[shell_mapping, shell_count]{Geometry::get_mapping(
        layer_sizes,
        [&](const std::size_t layer_index,
            const std::size_t item_index) -> Geometry::MappingOperatorResult {
            const BoundedPerimeters &layer{perimeters[layer_index]};
            const BoundedPerimeters &next_layer{perimeters[layer_index + 1]};
            if (next_layer.empty()) {
                return std::nullopt;
            }

            BoundingBoxes next_layer_bounding_boxes;
            for (const BoundedPerimeter &bounded_perimeter : next_layer) {
                next_layer_bounding_boxes.emplace_back(bounded_perimeter.bounding_box);
            }

            const auto [perimeter_index, distance] = Geometry::pick_closest_bounding_box(
                layer[item_index].bounding_box, next_layer_bounding_boxes
            );

            if (distance > max_distance) {
                return std::nullopt;
            }
            return std::pair{perimeter_index, 1.0 / distance};
        }
    )};

    return Impl::map_to_shells(std::move(perimeters), shell_mapping, shell_count);
}
} // namespace Slic3r::Seams::Shells
