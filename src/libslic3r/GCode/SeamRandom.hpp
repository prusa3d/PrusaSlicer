#include <random>
#include <cstddef>
#include <optional>
#include <vector>

#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"

namespace Slic3r {
namespace Seams {
struct SeamChoice;
struct SeamPerimeterChoice;
}  // namespace Seams
}  // namespace Slic3r

namespace Slic3r::Seams::Random {
namespace Impl {
struct PerimeterSegment
{
    double begin{};
    double end{};
    std::size_t begin_index{};

    double length() const { return end - begin; }
};

struct Random
{
    std::mt19937 &random_engine;

    std::optional<SeamChoice> operator()(
        const Perimeters::Perimeter &perimeter,
        const Perimeters::PointType point_type,
        const Perimeters::PointClassification point_classification
    ) const;
};
}
std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    Perimeters::LayerPerimeters &&perimeters, const unsigned fixed_seed
);
}
