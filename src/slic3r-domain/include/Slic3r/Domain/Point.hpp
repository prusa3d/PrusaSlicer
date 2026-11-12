#pragma once

#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Types.hpp"
#include <oneapi/tbb/scalable_allocator.h>
#include <boost/polygon/segment_concept.hpp>

namespace Slic3r::Domain {

static constexpr double SCALING_FACTOR = 0.000001;

//FIXME Better to use an inline function with an explicit return type.
//inline coord_t scale_(double v) { return coord_t(floor(v / SCALING_FACTOR + 0.5f)); }
#define scale_(val) ((val) / Slic3r::Domain::SCALING_FACTOR)

constexpr auto SCALED_EPSILON = Slic3r::Domain::EPSILON / Slic3r::Domain::SCALING_FACTOR;

using Point = Vec2crd;

Point rotated(const Point& point, const double angle, const Point &center = Point::Zero());
Point rotated(const Point& point, const double cos_a, const double sin_a);

template<typename BaseType>
using PointsAllocator = tbb::scalable_allocator<BaseType>;
using Points = std::vector<Point, PointsAllocator<Point>>;
using VecOfPoints = std::vector<Points, PointsAllocator<Points>>;

// To be used by std::unordered_map, std::unordered_multimap and friends.
struct PointHash {
    size_t operator()(const Vec2crd &pt) const noexcept {
        return coord_t((89 * 31 + int64_t(pt.x())) * 31 + pt.y());
    }
};

}

namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Domain::Point> { using type = point_concept; };

    template <>
    struct point_traits<Slic3r::Domain::Point> {
        using coordinate_type = Slic3r::Domain::coord_t;

        static inline coordinate_type get(const Slic3r::Domain::Point& point, orientation_2d orient) {
            return static_cast<coordinate_type>(point((orient == HORIZONTAL) ? 0 : 1));
        }
    };

    template <>
    struct point_mutable_traits<Slic3r::Domain::Point> {
        using coordinate_type = Slic3r::Domain::coord_t;
        static inline void set(Slic3r::Domain::Point& point, orientation_2d orient, coordinate_type value) {
            point((orient == HORIZONTAL) ? 0 : 1) = value;
        }
        static inline Slic3r::Domain::Point construct(coordinate_type x_value, coordinate_type y_value) {
            return Slic3r::Domain::Point(x_value, y_value);
        }
    };
} }
