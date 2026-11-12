#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include <oneapi/tbb/scalable_allocator.h>
#include <assert.h>
#include <cstddef>
#include <vector>
#include <cmath>
#include <string>
#include <sstream>
#include <unordered_map>
#include <Eigen/Geometry>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <cassert>

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Math.hpp"
#include "libslic3r.h"
#include "LocalesUtils.hpp"

namespace Slic3r {

using Vector = Domain::Point;

// Base template for eigen derived vectors
template<int N, int M, class T>
using Mat = Eigen::Matrix<T, N, M, Eigen::DontAlign, N, M>;

template<int N, class T> using LegacyVec = Domain::Advanced::Vec<T, N>;

template<typename NumberType>
using DynVec = Eigen::Matrix<NumberType, Eigen::Dynamic, 1>;

// Eigen types, to replace the Slic3r's own types in the future.
// Vector types with a fixed point coordinate base type.
using Vec2crd = Domain::Vec2crd;
using Vec3crd = Domain::Vec3crd;

// Vector types with a double coordinate base type.
using Vec2f = Domain::Vec2f;
using Vec3f = Domain::Vec3f;
using Vec4f = Domain::Vec4f;
using Vec2d = Domain::Vec2d;
using Vec3d = Domain::Vec3d;
using Vec4d = Domain::Vec4d;

using PointPtrs      = std::vector<Domain::Point*>;
using PointConstPtrs = std::vector<const Domain::Point*>;
using Points3        = std::vector<Vec3crd>;
using Pointfs        = std::vector<Vec2d>;
using Vec2ds         = std::vector<Vec2d>;
using Pointf3s       = std::vector<Vec3d>;


using Matrix2f       = Eigen::Matrix<float,  2, 2, Eigen::DontAlign>;
using Matrix2d       = Eigen::Matrix<double, 2, 2, Eigen::DontAlign>;
using Matrix3f       = Eigen::Matrix<float,  3, 3, Eigen::DontAlign>;
using Matrix3d       = Eigen::Matrix<double, 3, 3, Eigen::DontAlign>;
using Matrix4f       = Eigen::Matrix<float,  4, 4, Eigen::DontAlign>;
using Matrix4d       = Eigen::Matrix<double, 4, 4, Eigen::DontAlign>;

template<int N, class T>
using Transform = Eigen::Transform<float, N, Eigen::Affine, Eigen::DontAlign>;

using Transform2f    = Eigen::Transform<float,  2, Eigen::Affine, Eigen::DontAlign>;
using Transform2d    = Eigen::Transform<double, 2, Eigen::Affine, Eigen::DontAlign>;
using Transform3f    = Eigen::Transform<float,  3, Eigen::Affine, Eigen::DontAlign>;
using Transform3d    = Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign>;

// I don't know why Eigen::Transform::Identity() return a const object...
template<int N, class T> Transform<N, T> identity() { return Transform<N, T>::Identity(); }
inline const auto &identity3f = identity<3, float>;
inline const auto &identity3d = identity<3, double>;

// Cross product of two 2D vectors.
// None of the vectors may be of int32_t type as the result would overflow.
using Slic3r::cross2;

using Slic3r::perp;

// Angle from v1 to v2, returning double atan2(y, x) normalized to <-PI, PI>.
template<typename Derived, typename Derived2>
inline double angle(const Eigen::MatrixBase<Derived> &v1, const Eigen::MatrixBase<Derived2> &v2) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "angle(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "angle(): second parameter is not a 2D vector");
    auto v1d = v1.template cast<double>();
    auto v2d = v2.template cast<double>();
    return atan2(cross2(v1d, v2d), v1d.dot(v2d));
}

using Slic3r::Biz::Algorithms::Point::to_2d;
using Slic3r::Biz::Algorithms::Point::to_3d;

inline Vec2d   unscale(coord_t x, coord_t y) { return Vec2d(unscale<double>(x), unscale<double>(y)); }
inline Vec2d   unscale(const Vec2crd &pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec2d   unscale(const Vec2d   &pt) { return Vec2d(unscale<double>(pt.x()), unscale<double>(pt.y())); }
inline Vec3d   unscale(coord_t x, coord_t y, coord_t z) { return Vec3d(unscale<double>(x), unscale<double>(y), unscale<double>(z)); }
inline Vec3d   unscale(const Vec3crd &pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }
inline Vec3d   unscale(const Vec3d   &pt) { return Vec3d(unscale<double>(pt.x()), unscale<double>(pt.y()), unscale<double>(pt.z())); }

inline std::string to_string(const Vec2crd &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]"; }
inline std::string to_string(const Vec2d   &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + "]"; }
inline std::string to_string(const Vec3crd &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " + float_to_string_decimal_point(pt.z()) + "]"; }
inline std::string to_string(const Vec3d   &pt) { return std::string("[") + float_to_string_decimal_point(pt.x()) + ", " + float_to_string_decimal_point(pt.y()) + ", " + float_to_string_decimal_point(pt.z()) + "]"; }

std::vector<Vec3f> transform(const std::vector<Vec3f>& points, const Transform3f& t);
Pointf3s transform(const Pointf3s& points, const Transform3d& t);

/**
@brief Check whether transformation matrix contains odd number of mirroring.
NOTE: In code is sometime function named is_left_handed
@param transform Transformation to check
@return Is positive determinant
*/
inline bool has_reflection(const Transform3d &transform) { return transform.matrix().determinant() < 0; }

/**
@brief Getter on base of transformation matrix
@param index column index
@param transform source transformation
@return Base of transformation matrix
*/
inline const Vec3d get_base(unsigned index, const Transform3d &transform) { return transform.linear().col(index); }
inline const Vec3d get_x_base(const Transform3d &transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d &transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d &transform) { return get_base(2, transform); }
inline const Vec3d get_base(unsigned index, const Transform3d::LinearPart &transform) { return transform.col(index); }
inline const Vec3d get_x_base(const Transform3d::LinearPart &transform) { return get_base(0, transform); }
inline const Vec3d get_y_base(const Transform3d::LinearPart &transform) { return get_base(1, transform); }
inline const Vec3d get_z_base(const Transform3d::LinearPart &transform) { return get_base(2, transform); }

using Domain::Point;
using Domain::Points;

using VecOfPoints = std::vector<Points, Domain::PointsAllocator<Points>>;

inline bool is_approx(const Point &p1, const Point &p2, coord_t epsilon = coord_t(SCALED_EPSILON))
{
	Point d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2f &p1, const Vec2f &p2, float epsilon = float(EPSILON))
{
	Vec2f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec2d &p1, const Vec2d &p2, double epsilon = EPSILON)
{
	Vec2d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon;
}

inline bool is_approx(const Vec3f &p1, const Vec3f &p2, float epsilon = float(EPSILON))
{
	Vec3f d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline bool is_approx(const Vec3d &p1, const Vec3d &p2, double epsilon = EPSILON)
{
	Vec3d d = (p2 - p1).cwiseAbs();
	return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

inline bool is_approx(const Matrix3d &m1, const Matrix3d &m2, double epsilon = EPSILON)
{
    for (size_t i = 0; i < 3; i++)
        for (size_t j = 0; j < 3; j++)
            if (!is_approx(m1(i, j), m2(i, j), epsilon))
                return false;

    return true;
}

inline Point lerp(const Point &a, const Point &b, double t)
{
    assert((t >= -EPSILON) && (t <= 1. + EPSILON));
    return ((1. - t) * a.cast<double>() + t * b.cast<double>()).cast<coord_t>();
}

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary = false>
BoundingBox get_extents(const Points &pts);
extern template BoundingBox get_extents<false>(const Points &pts);
extern template BoundingBox get_extents<true>(const Points &pts);

// if IncludeBoundary, then a bounding box is defined even for a single point.
// otherwise a bounding box is only defined if it has a positive area.
template<bool IncludeBoundary = false>
BoundingBox get_extents(const VecOfPoints &pts);
extern template BoundingBox get_extents<false>(const VecOfPoints &pts);
extern template BoundingBox get_extents<true>(const VecOfPoints &pts);

BoundingBoxf get_extents(const std::vector<Vec2d> &pts);

int nearest_point_index(const Points &points, const Point &pt);

inline std::pair<Point, bool> nearest_point(const Points &points, const Point &pt)
{
    int idx = nearest_point_index(points, pt);
    return idx == -1 ? std::make_pair(Point(), false) : std::make_pair(points[idx], true);
}

// Test for duplicate points in a vector of points.
// The points are copied, sorted and checked for duplicates globally.
bool        has_duplicate_points(Points &&pts);
inline bool has_duplicate_points(const Points &pts)
{
    Points cpy = pts;
    return has_duplicate_points(std::move(cpy));
}

// Test for duplicate points in a vector of points.
// Only successive points are checked for equality.
inline bool has_duplicate_successive_points(const Points &pts)
{
    for (size_t i = 1; i < pts.size(); ++ i)
        if (pts[i - 1] == pts[i])
            return true;
    return false;
}

// Test for duplicate points in a vector of points.
// Only successive points are checked for equality. Additionally, first and last points are compared for equality.
inline bool has_duplicate_successive_points_closed(const Points &pts)
{
    return has_duplicate_successive_points(pts) || (pts.size() >= 2 && pts.front() == pts.back());
}

using Slic3r::Biz::Algorithms::Point::collect_duplicates;

inline bool shorter_then(const Point& p0, const coord_t len)
{
    if (p0.x() > len || p0.x() < -len)
        return false;
    if (p0.y() > len || p0.y() < -len)
        return false;
    return p0.cast<int64_t>().squaredNorm() <= Slic3r::sqr(int64_t(len));
}

// A generic class to search for a closest Point in a given radius.
// It uses std::unordered_multimap to implement an efficient 2D spatial hashing.
// The PointAccessor has to return const Point*.
// If a nullptr is returned, it is ignored by the query.
template<typename ValueType, typename PointAccessor> class ClosestPointInRadiusLookup
{
public:
    ClosestPointInRadiusLookup(coord_t search_radius, PointAccessor point_accessor = PointAccessor()) : 
		m_search_radius(search_radius), m_point_accessor(point_accessor), m_grid_log2(0)
    {
        // Resolution of a grid, twice the search radius + some epsilon.
		coord_t gridres = 2 * m_search_radius + 4;
        m_grid_resolution = gridres;
        assert(m_grid_resolution > 0);
        assert(m_grid_resolution < (coord_t(1) << 30));
		// Compute m_grid_log2 = log2(m_grid_resolution)
		if (m_grid_resolution > 32767) {
			m_grid_resolution >>= 16;
			m_grid_log2 += 16;
		}
		if (m_grid_resolution > 127) {
			m_grid_resolution >>= 8;
			m_grid_log2 += 8;
		}
		if (m_grid_resolution > 7) {
			m_grid_resolution >>= 4;
			m_grid_log2 += 4;
		}
		if (m_grid_resolution > 1) {
			m_grid_resolution >>= 2;
			m_grid_log2 += 2;
		}
		if (m_grid_resolution > 0)
			++ m_grid_log2;
		m_grid_resolution = 1 << m_grid_log2;
		assert(m_grid_resolution >= gridres);
		assert(gridres > m_grid_resolution / 2);
    }

    void insert(const ValueType &value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), value));
    }

    void insert(ValueType &&value) {
        const Vec2crd *pt = m_point_accessor(value);
        if (pt != nullptr)
            m_map.emplace(std::make_pair(Vec2crd(pt->x()>>m_grid_log2, pt->y()>>m_grid_log2), std::move(value)));
    }

    // Erase a data point equal to value. (ValueType has to declare the operator==).
    // Returns true if the data point equal to value was found and removed.
    bool erase(const ValueType &value) {
        const Point *pt = m_point_accessor(value);
        if (pt != nullptr) {
            // Range of fragment starts around grid_corner, close to pt.
            auto range = m_map.equal_range(Point((*pt).x()>>m_grid_log2, (*pt).y()>>m_grid_log2));
            // Remove the first item.
            for (auto it = range.first; it != range.second; ++ it) {
                if (it->second == value) {
                    m_map.erase(it);
                    return true;
                }
            }
        }
        return false;
    }

    // Return a pair of <ValueType*, distance_squared>
    std::pair<const ValueType*, double> find(const Vec2crd &pt) {
        // Iterate over 4 closest grid cells around pt,
        // find the closest start point inside these cells to pt.
        const ValueType *value_min = nullptr;
        double           dist_min = std::numeric_limits<double>::max();
        // Round pt to a closest grid_cell corner.
        Vec2crd            grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);
        // For four neighbors of grid_corner:
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 < dist_min) {
                            dist_min = d2;
                            value_min = &value;
                        }
                    }
                }
            }
        }
        return (value_min != nullptr && dist_min < double(m_search_radius) * double(m_search_radius)) ? 
            std::make_pair(value_min, dist_min) : 
            std::make_pair(nullptr, std::numeric_limits<double>::max());
    }

    // Returns all pairs of values and squared distances.
    std::vector<std::pair<const ValueType*, double>> find_all(const Vec2crd &pt) {
        // Iterate over 4 closest grid cells around pt,
        // Round pt to a closest grid_cell corner.
        Vec2crd      grid_corner((pt.x()+(m_grid_resolution>>1))>>m_grid_log2, (pt.y()+(m_grid_resolution>>1))>>m_grid_log2);
        // For four neighbors of grid_corner:
        std::vector<std::pair<const ValueType*, double>> out;
        const double r2 = double(m_search_radius) * m_search_radius;
        for (coord_t neighbor_y = -1; neighbor_y < 1; ++ neighbor_y) {
            for (coord_t neighbor_x = -1; neighbor_x < 1; ++ neighbor_x) {
                // Range of fragment starts around grid_corner, close to pt.
                auto range = m_map.equal_range(Vec2crd(grid_corner.x() + neighbor_x, grid_corner.y() + neighbor_y));
                // Find the map entry closest to pt.
                for (auto it = range.first; it != range.second; ++it) {
                    const ValueType &value = it->second;
                    const Vec2crd *pt2 = m_point_accessor(value);
                    if (pt2 != nullptr) {
                        const double d2 = (pt - *pt2).cast<double>().squaredNorm();
                        if (d2 <= r2)
                            out.emplace_back(&value, d2);
                    }
                }
            }
        }
        return out;
    }

private:
    using map_type = typename std::unordered_multimap<Vec2crd, ValueType, Domain::PointHash>;
    PointAccessor m_point_accessor;
    map_type m_map;
    coord_t  m_search_radius;
    coord_t  m_grid_resolution;
    coord_t  m_grid_log2;
};

std::ostream& operator<<(std::ostream &stm, const Vec2d &pointf);


// /////////////////////////////////////////////////////////////////////////////
// Type safe conversions to and from scaled and unscaled coordinates
// /////////////////////////////////////////////////////////////////////////////

// Semantics are the following:
// Upscaling (scaled()): only from floating point types (or Vec) to either
//                       floating point or integer 'scaled coord' coordinates.
// Downscaling (unscaled()): from arithmetic (or Vec) to floating point only

// Conversion definition from unscaled to floating point scaled
template<class Tout,
         class Tin,
         class = FloatingOnly<Tin>>
inline constexpr FloatingOnly<Tout> scaled(const Tin &v) noexcept
{
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion definition from unscaled to integer 'scaled coord'.
// TODO: is the rounding necessary? Here it is commented  out to show that
// it can be different for integers but it does not have to be. Using
// std::round means loosing noexcept and constexpr modifiers
template<class Tout = coord_t, class Tin, class = FloatingOnly<Tin>>
inline constexpr ScaledCoordOnly<Tout> scaled(const Tin &v) noexcept
{
    //return static_cast<Tout>(std::round(v / SCALING_FACTOR));
    return Tout(v / Tin(SCALING_FACTOR));
}

// Conversion for Eigen vectors (N dimensional points)
template<class Tout = coord_t,
         class Tin,
         int N,
         class = FloatingOnly<Tin>,
         int...EigenArgs>
inline Eigen::Matrix<ArithmeticOnly<Tout>, N, EigenArgs...>
scaled(const Eigen::Matrix<Tin, N, EigenArgs...> &v)
{
    return (v / SCALING_FACTOR).template cast<Tout>();
}

// Conversion from arithmetic scaled type to floating point unscaled
template<class Tout = double,
         class Tin,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>>
inline constexpr Tout unscaled(const Tin &v) noexcept
{
    return Tout(v) * Tout(SCALING_FACTOR);
}

// Unscaling for Eigen vectors. Input base type can be arithmetic, output base
// type can only be floating point.
template<class Tout = double,
         class Tin,
         int N,
         class = ArithmeticOnly<Tin>,
         class = FloatingOnly<Tout>,
         int...EigenArgs>
inline constexpr Eigen::Matrix<Tout, N, EigenArgs...>
unscaled(const Eigen::Matrix<Tin, N, EigenArgs...> &v) noexcept
{
    return v.template cast<Tout>() * Tout(SCALING_FACTOR);
}

// Align a coordinate to a grid. The coordinate may be negative,
// the aligned value will never be bigger than the original one.
inline coord_t align_to_grid(const coord_t coord, const coord_t spacing) {
    // Current C++ standard defines the result of integer division to be rounded to zero,
    // for both positive and negative numbers. Here we want to round down for negative
    // numbers as well.
    coord_t aligned = (coord < 0) ?
            ((coord - spacing + 1) / spacing) * spacing :
            (coord / spacing) * spacing;
    assert(aligned <= coord);
    return aligned;
}
inline Point   align_to_grid(Point   coord, Point   spacing) 
    { return Point(align_to_grid(coord.x(), spacing.x()), align_to_grid(coord.y(), spacing.y())); }
inline coord_t align_to_grid(coord_t coord, coord_t spacing, coord_t base) 
    { return base + align_to_grid(coord - base, spacing); }
inline Point   align_to_grid(Point   coord, Point   spacing, Point   base)
    { return Point(align_to_grid(coord.x(), spacing.x(), base.x()), align_to_grid(coord.y(), spacing.y(), base.y())); }

// MinMaxLimits
template<typename T> struct MinMax { T min; T max;};
template<typename T>
static bool apply(std::optional<T> &val, const MinMax<T> &limit) {
    if (!val.has_value()) return false;
    return apply<T>(*val, limit);
}
template<typename T>
static bool apply(T &val, const MinMax<T> &limit)
{
    if (val > limit.max) {
        val = limit.max;
        return true;
    }
    if (val < limit.min) {
        val = limit.min;
        return true;
    }
    return false;
}

} // namespace Slic3r

// To be able to use Vec<> and Mat<> in range based for loops:
namespace Eigen {
template<class T, int N, int M>
T* begin(Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
T* end(Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }

template<class T, int N, int M>
const T* begin(const Slic3r::Mat<N, M, T> &mat) { return mat.data(); }

template<class T, int N, int M>
const T* end(const Slic3r::Mat<N, M, T> &mat) { return mat.data() + N * M; }
} // namespace Eigen

#endif
