#ifndef slic3r_Geometry_ArcWelder_hpp_
#define slic3r_Geometry_ArcWelder_hpp_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <optional>
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>
#include <cinttypes>
#include <complex>
#include <cstddef>

#include "../Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace Geometry { namespace ArcWelder {

// Calculate center point (center of a circle) of an arc given two points and a radius.
// positive radius: take shorter arc
// negative radius: take longer arc
// radius must NOT be zero!
template<typename Derived, typename Derived2, typename Float>
inline Eigen::Matrix<Float, 2, 1, Eigen::DontAlign> arc_center(
    const Eigen::MatrixBase<Derived>   &start_pos,
    const Eigen::MatrixBase<Derived2>  &end_pos, 
    const Float                         radius,
    const bool                          is_ccw)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "arc_center(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "arc_center(): second parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "arc_center(): Both vectors must be of the same type.");
    static_assert(std::is_same<typename Derived::Scalar, Float>::value, "arc_center(): Radius must be of the same type as the vectors.");
    assert(radius != 0);
    using Vector = Eigen::Matrix<Float, 2, 1, Eigen::DontAlign>;
    auto  v  = end_pos - start_pos;
    Float q2 = v.squaredNorm();
    assert(q2 > 0);
    Float t2 = sqr(radius) / q2 - Float(.25f);
    // If the start_pos and end_pos are nearly antipodal, t2 may become slightly negative.
    // In that case return a centroid of start_point & end_point.
    Float t = t2 > 0 ? sqrt(t2) : Float(0);
    auto mid = Float(0.5) * (start_pos + end_pos);
    Vector vp{ -v.y() * t, v.x() * t };
    return (radius > Float(0)) == is_ccw ? (mid + vp).eval() : (mid - vp).eval();
}

// Calculate middle sample point (point on an arc) of an arc given two points and a radius.
// positive radius: take shorter arc
// negative radius: take longer arc
// radius must NOT be zero!
// Taking a sample at the middle of a convex arc (less than PI) is likely much more
// useful than taking a sample at the middle of a concave arc (more than PI).
template<typename Derived, typename Derived2, typename Float>
inline Eigen::Matrix<Float, 2, 1, Eigen::DontAlign> arc_middle_point(
    const Eigen::MatrixBase<Derived>  &start_pos,
    const Eigen::MatrixBase<Derived2> &end_pos,
    const Float                        radius,
    const bool                         is_ccw)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "arc_center(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "arc_center(): second parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "arc_center(): Both vectors must be of the same type.");
    static_assert(std::is_same<typename Derived::Scalar, Float>::value, "arc_center(): Radius must be of the same type as the vectors.");
    assert(radius != 0);
    using Vector = Eigen::Matrix<Float, 2, 1, Eigen::DontAlign>;
    auto  v = end_pos - start_pos;
    Float q2 = v.squaredNorm();
    assert(q2 > 0);
    Float t2 = sqr(radius) / q2 - Float(.25f);
    // If the start_pos and end_pos are nearly antipodal, t2 may become slightly negative.
    // In that case return a centroid of start_point & end_point.
    Float t = (t2 > 0 ? sqrt(t2) : Float(0)) - radius / sqrt(q2);
    auto mid = Float(0.5) * (start_pos + end_pos);
    Vector vp{ -v.y() * t, v.x() * t };
    return (radius > Float(0)) == is_ccw ? (mid + vp).eval() : (mid - vp).eval();
}

// Calculate angle of an arc given two points and a radius.
// Returned angle is in the range <0, 2 PI)
// positive radius: take shorter arc
// negative radius: take longer arc
// radius must NOT be zero!
template<typename Derived, typename Derived2>
inline typename Derived::Scalar arc_angle(
    const Eigen::MatrixBase<Derived>   &start_pos,
    const Eigen::MatrixBase<Derived2>  &end_pos, 
    const typename Derived::Scalar      radius)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "arc_angle(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "arc_angle(): second parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "arc_angle(): Both vectors must be of the same type.");
    assert(radius != 0);
    using Float = typename Derived::Scalar;
    Float a = Float(0.5) * (end_pos - start_pos).norm() / radius;
    return radius > Float(0) ?
        // acute angle:
        (a > Float( 1.) ? Float(M_PI) : Float(2.) * std::asin(a)) :
        // obtuse angle:
        (a < Float(-1.) ? Float(M_PI) : Float(2. * M_PI) + Float(2.) * std::asin(a));
}

// Calculate positive length of an arc given two points and a radius.
// positive radius: take shorter arc
// negative radius: take longer arc
// radius must NOT be zero!
template<typename Derived, typename Derived2>
inline typename Derived::Scalar arc_length(
    const Eigen::MatrixBase<Derived>   &start_pos,
    const Eigen::MatrixBase<Derived2>  &end_pos,
    const typename Derived::Scalar      radius)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "arc_length(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "arc_length(): second parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "arc_length(): Both vectors must be of the same type.");
    assert(radius != 0);
    return arc_angle(start_pos, end_pos, radius) * std::abs(radius);
}

// Calculate positive length of an arc given two points, center and orientation.
template<typename Derived, typename Derived2, typename Derived3>
inline typename Derived::Scalar arc_length(
    const Eigen::MatrixBase<Derived>   &start_pos,
    const Eigen::MatrixBase<Derived2>  &end_pos,
    const Eigen::MatrixBase<Derived3>  &center_pos,
    const bool                          ccw)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "arc_length(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "arc_length(): second parameter is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "arc_length(): third parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value &&
                  std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value, "arc_length(): All third points must be of the same type.");
    using Float = typename Derived::Scalar;
    auto  vstart = start_pos - center_pos;
    auto  vend   = end_pos - center_pos;
    Float radius = vstart.norm();
    Float angle  = atan2(double(cross2(vstart, vend)), double(vstart.dot(vend)));
    if (! ccw)
        angle *= Float(-1.);
    if (angle < 0)
        angle += Float(2. * M_PI);
    assert(angle >= Float(0.) && angle < Float(2. * M_PI + EPSILON));
    return angle * radius;
}

// Be careful! This version has a strong bias towards small circles with small radii
// for small angle (nearly straight) arches!
// One should rather use arc_fit_center_gauss_newton_ls(), which solves a non-linear least squares problem.
//
// Calculate center point (center of a circle) of an arc given two fixed points to interpolate
// and an additional list of points to fit by least squares.
// The circle fitting problem is non-linear, it was linearized by taking difference of squares of radii as a residual.
// Therefore the center point is required as a point to linearize at.
// Start & end point must be different and the interpolated points must not be collinear with input points.
template<typename Derived, typename Derived2, typename Derived3, typename Iterator>
inline typename Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> arc_fit_center_algebraic_ls(
    const Eigen::MatrixBase<Derived>   &start_pos,
    const Eigen::MatrixBase<Derived2>  &end_pos,
    const Eigen::MatrixBase<Derived3>  &center_pos,
    const Iterator                      it_begin,
    const Iterator                      it_end)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "arc_fit_center_algebraic_ls(): start_pos is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "arc_fit_center_algebraic_ls(): end_pos is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "arc_fit_center_algebraic_ls(): third parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value &&
                  std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value, "arc_fit_center_algebraic_ls(): All third points must be of the same type.");
    using Float  = typename Derived::Scalar;
    using Vector = Eigen::Matrix<Float, 2, 1, Eigen::DontAlign>;
    // Prepare a vector space to transform the fitting into:
    // center_pos, dir_x, dir_y
    Vector v     = end_pos - start_pos;
    Vector c     = Float(.5) * (start_pos + end_pos);
    Float  lv    = v.norm();
    assert(lv > 0);
    Vector dir_y = v / lv;
    Vector dir_x = perp(dir_y);
    // Center of the new system:
    // Center X at the projection of center_pos
    Float  offset_x = dir_x.dot(center_pos);
    // Center is supposed to lie on bisector of the arc end points.
    // Center Y at the mid point of v.
    Float  offset_y = dir_y.dot(c);
    assert(std::abs(dir_y.dot(center_pos) - offset_y) < SCALED_EPSILON);
    assert((dir_x * offset_x + dir_y * offset_y - center_pos).norm() < SCALED_EPSILON);
    // Solve the least squares fitting in a transformed space.
    Float a     = Float(0.5) * lv;
    Float b     = c.dot(dir_x) - offset_x;
    Float ab2   = sqr(a) + sqr(b);
    Float num   = Float(0);
    Float denom = Float(0);
    const Float w = it_end - it_begin;
    for (Iterator it = it_begin; it != it_end; ++ it) {
        Vector p = *it;
        Float x_i = dir_x.dot(p) - offset_x;
        Float y_i = dir_y.dot(p) - offset_y;
        Float x_i2 = sqr(x_i);
        Float y_i2 = sqr(y_i);
        num   += (x_i - b) * (x_i2 + y_i2 - ab2);
        denom += b * (b - Float(2) * x_i) + sqr(x_i) + Float(0.25) * w;
    }
    assert(denom != 0);
    Float c_x = Float(0.5) * num / denom;
    // Transform the center back.
    Vector out = dir_x * (c_x + offset_x) + dir_y * offset_y;
    return out;
}

// Calculate center point (center of a circle) of an arc given two fixed points to interpolate
// and an additional list of points to fit by non-linear least squares.
// The non-linear least squares problem is solved by a Gauss-Newton iterative method.
// Start & end point must be different and the interpolated points must not be collinear with input points.
// Center position is used to calculate the initial solution of the Gauss-Newton method.
//
// In case the input points are collinear or close to collinear (such as a small angle arc),
// the solution may not converge and an error is indicated.
template<typename Derived, typename Derived2, typename Derived3, typename Iterator>
inline std::optional<typename Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign>> arc_fit_center_gauss_newton_ls(
    const Eigen::MatrixBase<Derived>   &start_pos,
    const Eigen::MatrixBase<Derived2>  &end_pos,
    const Eigen::MatrixBase<Derived3>  &center_pos,
    const Iterator                      it_begin,
    const Iterator                      it_end,
    const size_t                        num_iterations)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "arc_fit_center_gauss_newton_ls(): start_pos is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "arc_fit_center_gauss_newton_ls(): end_pos is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "arc_fit_center_gauss_newton_ls(): third parameter is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value &&
                  std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value, "arc_fit_center_gauss_newton_ls(): All third points must be of the same type.");
    using Float = typename Derived::Scalar;
    using Vector = Eigen::Matrix<Float, 2, 1, Eigen::DontAlign>;
    // Prepare a vector space to transform the fitting into:
    // center_pos, dir_x, dir_y
    Vector v = end_pos - start_pos;
    Vector c = Float(.5) * (start_pos + end_pos);
    Float  lv = v.norm();
    assert(lv > 0);
    Vector dir_y = v / lv;
    Vector dir_x = perp(dir_y);
    // Center is supposed to lie on bisector of the arc end points.
    // Center Y at the mid point of v.
    Float  offset_y = dir_y.dot(c);
    // Initial value of the parameter to be optimized iteratively.
    Float  c_x = dir_x.dot(center_pos);
    // Solve the least squares fitting in a transformed space.
    Float  a  = Float(0.5) * lv;
    Float  a2 = sqr(a);
    Float  b  = c.dot(dir_x);
    for (size_t iter = 0; iter < num_iterations; ++ iter) {
        Float num   = Float(0);
        Float denom = Float(0);
        Float u     = b - c_x;
        // Current estimate of the circle radius.
        Float r = sqrt(a2 + sqr(u));
        assert(r > 0);
        for (Iterator it = it_begin; it != it_end; ++ it) {
            Vector p = *it;
            Float x_i = dir_x.dot(p);
            Float y_i = dir_y.dot(p) - offset_y;
            Float v   = x_i - c_x;
            Float y_i2 = sqr(y_i);
            // Distance of i'th sample from the current circle center.
            Float r_i = sqrt(sqr(v) + y_i2);
            if (r_i >= EPSILON) {
                // Square of residual is differentiable at the current c_x and current sample.
                // Jacobian: diff(residual, c_x)
                Float j_i = u / r - v / r_i;
                num   += j_i * (r_i - r);
                denom += sqr(j_i);
            } else {
                // Sample point is on current center of the circle,
                // therefore the gradient is not defined.
            }
        }
        if (denom == 0)
            // Fitting diverged, the input points are likely nearly collinear with the arch end points.
            return std::optional<Vector>();
        c_x -= num / denom;
    }
    // Transform the center back.
    return std::optional<Vector>(dir_x * c_x + dir_y * offset_y);
}

// Test whether a point is inside a wedge of an arc.
template<typename Derived, typename Derived2, typename Derived3>
inline bool inside_arc_wedge_vectors(
    const Eigen::MatrixBase<Derived>    &start_vec,
    const Eigen::MatrixBase<Derived2>   &end_vec,
    const bool                           shorter_arc,
    const bool                           ccw,
    const Eigen::MatrixBase<Derived3>   &query_vec)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "inside_arc_wedge_vectors(): start_vec is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "inside_arc_wedge_vectors(): end_vec is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "inside_arc_wedge_vectors(): query_vec is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value &&
                  std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value, "inside_arc_wedge_vectors(): All vectors must be of the same type.");
    return shorter_arc ?
        // Smaller (convex) wedge.
        (ccw ?
            cross2(start_vec, query_vec) > 0 && cross2(query_vec, end_vec) > 0 :
            cross2(start_vec, query_vec) < 0 && cross2(query_vec, end_vec) < 0) :
        // Larger (concave) wedge.
        (ccw ?
            cross2(end_vec, query_vec) < 0 || cross2(query_vec, start_vec) < 0 :
            cross2(end_vec, query_vec) > 0 || cross2(query_vec, start_vec) > 0);
}

template<typename Derived, typename Derived2, typename Derived3, typename Derived4>
inline bool inside_arc_wedge(
    const Eigen::MatrixBase<Derived>    &start_pt,
    const Eigen::MatrixBase<Derived2>   &end_pt,
    const Eigen::MatrixBase<Derived3>   &center_pt,
    const bool                           shorter_arc,
    const bool                           ccw,
    const Eigen::MatrixBase<Derived4>   &query_pt)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "inside_arc_wedge(): start_pt is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "inside_arc_wedge(): end_pt is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "inside_arc_wedge(): center_pt is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived4::SizeAtCompileTime) == 2, "inside_arc_wedge(): query_pt is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value &&
        std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value &&
        std::is_same<typename Derived::Scalar, typename Derived4::Scalar>::value, "inside_arc_wedge(): All vectors must be of the same type.");
    return inside_arc_wedge_vectors(start_pt - center_pt, end_pt - center_pt, shorter_arc, ccw, query_pt - center_pt);
}

template<typename Derived, typename Derived2, typename Derived3, typename Float>
inline bool inside_arc_wedge(
    const Eigen::MatrixBase<Derived>    &start_pt,
    const Eigen::MatrixBase<Derived2>   &end_pt,
    const Float                          radius,
    const bool                           ccw,
    const Eigen::MatrixBase<Derived3>   &query_pt)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "inside_arc_wedge(): start_pt is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "inside_arc_wedge(): end_pt is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "inside_arc_wedge(): query_pt is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value &&
        std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value &&
        std::is_same<typename Derived::Scalar, Float>::value, "inside_arc_wedge(): All vectors + radius must be of the same type.");
    return inside_arc_wedge(start_pt, end_pt,
        arc_center(start_pt, end_pt, radius, ccw), 
        radius > 0, ccw, query_pt);
}

// Return number of linear segments necessary to interpolate arc of a given positive radius and positive angle to satisfy
// maximum deviation of an interpolating polyline from an analytic arc.
template<typename FloatType>
size_t arc_discretization_steps(const FloatType radius, const FloatType angle, const FloatType deviation)
{
    assert(radius > 0);
    assert(angle > 0);
    assert(angle <= FloatType(2. * M_PI));
    assert(deviation > 0);

    FloatType d = radius - deviation;
    return d < EPSILON ?
        // Radius smaller than deviation.
        (   // Acute angle: a single segment interpolates the arc with sufficient accuracy.
            angle < M_PI || 
            // Obtuse angle: Test whether the furthest point (center) of an arc is closer than deviation to the center of a line segment.
            radius * (FloatType(1.) + cos(M_PI - FloatType(.5) * angle)) < deviation ?
            // Single segment is sufficient
            1 :
            // Two segments are necessary, the middle point is at the center of the arc.
            2) :
        size_t(ceil(angle / (2. * acos(d / radius))));
}

// Discretize arc given the radius, orientation and maximum deviation from the arc.
// Returned polygon starts with p1, ends with p2 and it is discretized to guarantee the maximum deviation.
Points arc_discretize(const Point &p1, const Point &p2, const double radius, const bool ccw, const double deviation);

// Variance of the arc fit of points <begin, end).
// First and last points of <begin, end) are expected to fit the arc exactly.
double arc_fit_variance(const Point &start_point, const Point &end_point, const float radius, bool is_ccw,
    const Points::const_iterator begin, const Points::const_iterator end);

// Maximum signed deviation of points <begin, end) (L1) from the arc.
// First and last points of <begin, end) are expected to fit the arc exactly.
double arc_fit_max_deviation(const Point &start_point, const Point &end_point, const float radius, bool is_ccw,
    const Points::const_iterator begin, const Points::const_iterator end);

// 1.2m diameter, maximum given by coord_t
static_assert(sizeof(coord_t) == 4);
static constexpr const double default_scaled_max_radius = scaled<double>(600.);
// 0.05mm
static constexpr const double default_scaled_resolution = scaled<double>(0.05);
// 5 percent
static constexpr const double default_arc_length_percent_tolerance = 0.05;

enum class Orientation : unsigned char {
    Unknown,
    CCW,
    CW,
};

// Returns orientation of a polyline with regard to the center.
// Successive points are expected to take less than a PI angle step.
// Returns Orientation::Unknown if the orientation with regard to the center 
// is not monotonous.
Orientation arc_orientation(
    const Point                 &center,
    const Points::const_iterator begin,
    const Points::const_iterator end);

// Single segment of a smooth path.
struct Segment
{
    // End point of a linear or circular segment.
    // Start point is provided by the preceding segment.
    Point       point;
    // Radius of a circular segment. Positive - take the shorter arc. Negative - take the longer arc. Zero - linear segment.
    float       radius{ 0.f };
    // CCW or CW. Ignored for zero radius (linear segment).
    Orientation orientation{ Orientation::CCW };

    float height_fraction{ 1.f };
    float e_fraction{ 1.f };

    bool    linear() const { return radius == 0; }
    bool    ccw() const { return orientation == Orientation::CCW; }
    bool    cw() const { return orientation == Orientation::CW; }
};

inline bool operator==(const Segment &lhs, const Segment &rhs) {
    return lhs.point == rhs.point && lhs.radius == rhs.radius && lhs.orientation == rhs.orientation;
}

using Segments = std::vector<Segment>;
using Path = Segments;

// Interpolate polyline path with a sequence of linear / circular segments given the interpolation tolerance.
// Only convert to polyline if zero tolerance.
// Convert to polyline and decimate polyline if zero fit_circle_percent_tolerance.
// Path fitting is inspired with the arc fitting algorithm in
//      Arc Welder: Anti-Stutter Library by Brad Hochgesang FormerLurker@pm.me
//      https://github.com/FormerLurker/ArcWelderLib 
Path fit_path(const Points &points, double tolerance, double fit_circle_percent_tolerance);

// Decimate polyline into a smooth path structure using Douglas-Peucker polyline decimation algorithm.
inline Path fit_polyline(const Points &points, double tolerance) { return fit_path(points, tolerance, 0.); }

template<typename FloatType>
inline FloatType segment_length(const Segment &start, const Segment &end)
{
    return end.linear() ?
        (end.point - start.point).cast<FloatType>().norm() :
        arc_length(start.point.cast<FloatType>(), end.point.cast<FloatType>(), FloatType(end.radius));
}

template<typename FloatType>
inline FloatType path_length(const Path &path)
{
    FloatType len = 0;
    for (size_t i = 1; i < path.size(); ++ i)
        len += segment_length<FloatType>(path[i - 1], path[i]);
    return len;
}

// Estimate minimum path length of a segment cheaply without having to calculate center of an arc and it arc length.
// Used for caching a smooth path chunk that is certainly longer than a threshold.
inline int64_t estimate_min_segment_length(const Segment &start, const Segment &end)
{
    if (end.linear() || end.radius > 0) {
        // Linear segment or convex wedge, take the larger X or Y component.
        Point v = (end.point - start.point).cwiseAbs();
        return std::max(v.x(), v.y());
    } else {
        // Arc with angle > PI.
        // Returns estimate of PI * r
        return - int64_t(3) * int64_t(end.radius);
    }
}

// Estimate minimum path length cheaply without having to calculate center of an arc and it arc length.
// Used for caching a smooth path chunk that is certainly longer than a threshold.
inline int64_t estimate_path_length(const Path &path)
{
    int64_t len = 0;
    for (size_t i = 1; i < path.size(); ++ i)
        len += Geometry::ArcWelder::estimate_min_segment_length(path[i - 1], path[i]);
    return len;
}

void reverse(Path &path);

// Clip start / end of a smooth path by len.
// If path is shorter than len, remaining path length to trim will be returned.
double clip_start(Path &path, const double len);
double clip_end(Path &path, const double len);

struct PathSegmentProjection
{
    // Start segment of a projection on the path.
    size_t segment_id { std::numeric_limits<size_t>::max() };
    // Projection of the point on the segment.
    Point  point      { 0, 0 };
    // If the point lies on an arc, the arc center is cached here.
    Point  center     { 0, 0 };
    // Square of a distance of the projection.
    double distance2  { std::numeric_limits<double>::max() };

    bool   valid() const { return this->segment_id != std::numeric_limits<size_t>::max(); }
};
// Returns closest segment and a parameter along the closest segment of a path to a point.
PathSegmentProjection point_to_path_projection(const Path &path, const Point &point, double search_radius2 = std::numeric_limits<double>::max());
// Split a path into two paths at a segment point. Snap to an existing point if the projection of "point is closer than min_segment_length.
std::pair<Path, Path> split_at(const Path &path, const PathSegmentProjection &proj, const double min_segment_length);
// Split a path into two paths at a point closest to "point". Snap to an existing point if the projection of "point is closer than min_segment_length.
std::pair<Path, Path> split_at(const Path &path, const Point &point, const double min_segment_length);

} } } // namespace Slic3r::Geometry::ArcWelder

#endif // slic3r_Geometry_ArcWelder_hpp_
