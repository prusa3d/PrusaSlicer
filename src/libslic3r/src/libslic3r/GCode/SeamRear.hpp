#ifndef libslic3r_SeamRear_hpp_
#define libslic3r_SeamRear_hpp_

#include <cstddef>
#include <vector>

#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/GCode/SeamChoice.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
namespace Seams {
namespace Perimeters {
struct BoundedPerimeter;
}  // namespace Perimeters
struct SeamPerimeterChoice;
}  // namespace Seams
}  // namespace Slic3r

namespace Slic3r::Seams::Rear {
namespace Impl {
struct PerimeterLine
{
    Vec2d a;
    Vec2d b;
    std::size_t previous_index;
    std::size_t next_index;

    using Scalar = Vec2d::Scalar;
    static const constexpr int Dim = 2;
};
} // namespace Impl

std::vector<std::vector<SeamPerimeterChoice>> get_object_seams(
    std::vector<std::vector<Perimeters::BoundedPerimeter>> &&perimeters,
    const double rear_tolerance,
    const double rear_y_offet
);
} // namespace Slic3r::Seams::Rear

#endif // libslic3r_SeamRear_hpp_
