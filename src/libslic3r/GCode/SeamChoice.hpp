#ifndef libslic3r_SeamChoice_hpp_
#define libslic3r_SeamChoice_hpp_

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#include "libslic3r/Polygon.hpp"
#include "libslic3r/GCode/SeamShells.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/SeamGeometry.hpp"
#include "libslic3r/GCode/SeamPerimeters.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r::Seams {

/**
 * When previous_index == next_index, the point is at the point.
 * Otherwise the point is at the edge.
 */
struct SeamChoice
{
    std::size_t previous_index{};
    std::size_t next_index{};
    Vec2d position{Vec2d::Zero()};
};

struct SeamPerimeterChoice
{
    SeamPerimeterChoice(const SeamChoice &choice, Perimeters::Perimeter &&perimeter)
        : choice(choice)
        , perimeter(std::move(perimeter))
        , bounding_box(Polygon{Geometry::scaled(this->perimeter.positions)}.bounding_box()) {}

    SeamChoice choice;
    Perimeters::Perimeter perimeter;
    BoundingBox bounding_box;
};

using SeamPicker = std::function<std::optional<
    SeamChoice>(const Perimeters::Perimeter &, const Perimeters::PointType, const Perimeters::PointClassification)>;

std::optional<SeamChoice> maybe_choose_seam_point(
    const Perimeters::Perimeter &perimeter, const SeamPicker &seam_picker
);

/**
 * Go throught points on perimeter and choose the best seam point closest to
 * the prefered position.
 *
 * Points in the perimeter can be diveded into 3x3=9 categories. An example category is
 * enforced overhanging point. These categories are searched in particualr order.
 * For example enforced overhang will be always choosen over common embedded point, etc.
 *
 * A closest point is choosen from the first non-empty category.
 */
SeamChoice choose_seam_point(
    const Perimeters::Perimeter &perimeter, const SeamPicker& seam_picker
);

std::optional<SeamChoice> choose_degenerate_seam_point(const Perimeters::Perimeter &perimeter);

} // namespace Slic3r::Seams

#endif // libslic3r_SeamChoice_hpp_
