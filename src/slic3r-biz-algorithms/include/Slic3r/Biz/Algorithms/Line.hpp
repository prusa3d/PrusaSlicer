#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Math.hpp"

namespace Slic3r::Biz::Algorithms::Line {

double distance_to_squared(const Domain::Line &line, const Domain::Point &point, Domain::Point &nearest_point_out);
double distance_to_squared(const Domain::Line &line, const Domain::Point &point);
double distance_to(const Domain::Line &line, const Domain::Point &point);

/**
 * Returns the squared distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
double distance_to_infinite_squared(const Domain::Line &line, const Domain::Point &point, Domain::Point &nearest_point_out);

/**
 * Returns the squared distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
double distance_to_infinite_squared(const Domain::Line &line, const Domain::Point &point);

/**
 * Returns the distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
double distance_to_infinite(const Domain::Line &line, const Domain::Point &point, Domain::Point &nearest_point_out);

/**
 * Returns the distance to the nearest point on the infinite segment.
 *
 * @note The nearest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
 */
double distance_to_infinite(const Domain::Line &line, const Domain::Point &point);

bool intersection(const Domain::Line& line, const Domain::Line& other_line, Domain::Point& intersection_point_out);
bool intersection_infinite(const Domain::Line& line, const Domain::Line& other_line, Domain::Point& intersection_point_out);

Domain::BoundingBox2crd get_extents(const Domain::Lines& lines);

Domain::Vec3d intersect_plane(const Domain::Line3d& line, double z);

Domain::Line3d transformed(const Domain::Line3d& line, const Domain::Transform3d& t);

namespace line_alg {

template<class L, class En = void> struct Traits {
    static constexpr int Dim = L::Dim;
    using Scalar = typename L::Scalar;

    static Domain::Advanced::Vec<Scalar, Dim>& get_a(L &l) { return l.a; }
    static Domain::Advanced::Vec<Scalar, Dim>& get_b(L &l) { return l.b; }
    static const Domain::Advanced::Vec<Scalar, Dim>& get_a(const L &l) { return l.a; }
    static const Domain::Advanced::Vec<Scalar, Dim>& get_b(const L &l) { return l.b; }
};

template<class L> const constexpr int Dim = Traits<std::remove_cvref_t<L>>::Dim;
template<class L> using Scalar = typename Traits<std::remove_cvref_t<L>>::Scalar;

template<class L> auto get_a(L &&l) { return Traits<std::remove_cvref_t<L>>::get_a(l); }
template<class L> auto get_b(L &&l) { return Traits<std::remove_cvref_t<L>>::get_b(l); }

// Distance to the closest point of line.
template<class L>
inline double distance_to_squared(const L &line, const Domain::Advanced::Vec<Scalar<L>, Dim<L>> &point, Domain::Advanced::Vec<Scalar<L>, Dim<L>> *nearest_point)
{
    using VecType = Domain::Advanced::Vec<double, Dim<L>>;
    const VecType  v  = (get_b(line) - get_a(line)).template cast<double>();
    const VecType  va = (point  - get_a(line)).template cast<double>();
    const double  l2 = v.squaredNorm();
    if (l2 == 0.0) {
        // a == b case
        *nearest_point = get_a(line);
        return va.squaredNorm();
    }
    // Consider the line extending the segment, parameterized as a + t (b - a).
    // We find projection of this point onto the line.
    // It falls where t = [(this-a) . (b-a)] / |b-a|^2
    const double t = va.dot(v);
    if (t <= 0.0) {
        // beyond the 'a' end of the segment
        *nearest_point = get_a(line);
        return va.squaredNorm();
    } else if (t >= l2) {
        // beyond the 'b' end of the segment
        *nearest_point = get_b(line);
        return (point - get_b(line)).template cast<double>().squaredNorm();
    }

    const VecType w = ((t / l2) * v).eval();
    *nearest_point = (get_a(line).template cast<double>() + w).template cast<Scalar<L>>();
    return (w - va).squaredNorm();
}

// Distance to the closest point of line.
template<class L>
double distance_to_squared(const L &line, const Domain::Advanced::Vec<Scalar<L>, Dim<L>> &point)
{
    Domain::Advanced::Vec<Scalar<L>, Dim<L>> nearest_point;
    return distance_to_squared<L>(line, point, &nearest_point);
}

template<class L>
double distance_to(const L &line, const Domain::Advanced::Vec<Scalar<L>, Dim<L>> &point)
{
    return std::sqrt(distance_to_squared(line, point));
}

// Returns a squared distance to the closest point on the infinite.
// Returned nearest_point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
template<class L>
double distance_to_infinite_squared(const L &line, const Domain::Advanced::Vec<Scalar<L>, Dim<L>> &point, Domain::Advanced::Vec<Scalar<L>, Dim<L>> *closest_point)
{
    const Domain::Advanced::Vec<double, Dim<L>> v  = (get_b(line) - get_a(line)).template cast<double>();
    const Domain::Advanced::Vec<double, Dim<L>> va = (point - get_a(line)).template cast<double>();
    const double              l2 = v.squaredNorm(); // avoid a sqrt
    if (l2 == 0.) {
        // a == b case
        *closest_point = get_a(line);
        return va.squaredNorm();
    }
    // Consider the line extending the segment, parameterized as a + t (b - a).
    // We find projection of this point onto the line.
    // It falls where t = [(this-a) . (b-a)] / |b-a|^2
    const double t = va.dot(v) / l2;
    *closest_point = (get_a(line).template cast<double>() + t * v).template cast<Scalar<L>>();
    return (t * v - va).squaredNorm();
}

// Returns a squared distance to the closest point on the infinite.
// Closest point (and returned squared distance to this point) could be beyond the 'a' and 'b' ends of the segment.
template<class L>
double distance_to_infinite_squared(const L &line, const Domain::Advanced::Vec<Scalar<L>, Dim<L>> &point)
{
    Domain::Advanced::Vec<Scalar<L>, Dim<L>> nearest_point;
    return distance_to_infinite_squared<L>(line, point, &nearest_point);
}

template<class L> bool intersection(const L &l1, const L &l2, Domain::Advanced::Vec<Scalar<L>, Dim<L>> *intersection_pt)
{
    using Floating      = typename std::conditional<std::is_floating_point<Scalar<L>>::value, Scalar<L>, double>::type;
    using VecType       = const Domain::Advanced::Vec<Floating, Dim<L>>;
    const VecType v1    = (l1.b - l1.a).template cast<Floating>();
    const VecType v2    = (l2.b - l2.a).template cast<Floating>();
    Floating      denom = cross2(v1, v2);
    if (fabs(denom) < Domain::EPSILON)
#if 0
        // Lines are collinear. Return true if they are coincident (overlappign).
        return ! (fabs(nume_a) < EPSILON && fabs(nume_b) < EPSILON);
#else
        return false;
#endif
    const VecType v12 = (l1.a - l2.a).template cast<Floating>();
    Floating nume_a = cross2(v2, v12);
    Floating nume_b = cross2(v1, v12);
    Floating t1     = nume_a / denom;
    Floating t2     = nume_b / denom;
    if (t1 >= 0 && t1 <= 1.0f && t2 >= 0 && t2 <= 1.0f) {
        // Get the intersection point.
        (*intersection_pt) = (l1.a.template cast<Floating>() + t1 * v1).template cast<Scalar<L>>();
        return true;
    }
    return false; // not intersecting
}

inline Domain::Point midpoint(const Domain::Point &a, const Domain::Point &b) {
    return (a + b) / 2;
}

}

} // namespace Slic3r::Biz::Algorithms::Line
