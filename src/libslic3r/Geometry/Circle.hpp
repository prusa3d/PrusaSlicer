///|/ Copyright (c) Prusa Research 2021 - 2022 Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Geometry_Circle_hpp_
#define slic3r_Geometry_Circle_hpp_

#include <assert.h>
#include <stddef.h>
#include <Eigen/Geometry>
#include <cmath>
#include <iterator>
#include <optional>
#include <type_traits>
#include <cassert>
#include <complex>
#include <cstddef>

#include "../Point.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Point.hpp"

namespace Slic3r { namespace Geometry {

// https://en.wikipedia.org/wiki/Circumscribed_circle
// Circumcenter coordinates, Cartesian coordinates
// In case the three points are collinear, returns their centroid.
template<typename Derived, typename Derived2, typename Derived3>
Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> circle_center(const Derived &a, const Derived2 &bsrc, const Derived3 &csrc, typename Derived::Scalar epsilon)
{
    static_assert(Derived ::IsVectorAtCompileTime && int(Derived ::SizeAtCompileTime) == 2, "circle_center(): 1st point is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "circle_center(): 2nd point is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "circle_center(): 3rd point is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value && std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value, 
        "circle_center(): All three points must be of the same type.");
    using Scalar = typename Derived::Scalar;
    using Vector = Eigen::Matrix<Scalar, 2, 1, Eigen::DontAlign>;
    Vector b  = bsrc - a;
    Vector c  = csrc - a;
	Scalar lb = b.squaredNorm();
	Scalar lc = c.squaredNorm();
    if (Scalar d = b.x() * c.y() - b.y() * c.x(); std::abs(d) < epsilon) {
    	// The three points are collinear. Take the center of the two points
    	// furthest away from each other.
    	Scalar lbc = (csrc - bsrc).squaredNorm();
		return Scalar(0.5) * (
			lb > lc && lb > lbc ? a + bsrc :
			lc > lb && lc > lbc ? a + csrc : bsrc + csrc);
    } else {
        Vector v = lc * b - lb * c;
        return a + Vector(- v.y(), v.x()) / (2 * d);
    }
}

// https://en.wikipedia.org/wiki/Circumscribed_circle
// Circumcenter coordinates, Cartesian coordinates
// Returns no value if the three points are collinear.
template<typename Derived, typename Derived2, typename Derived3>
std::optional<Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign>> try_circle_center(const Derived &a, const Derived2 &bsrc, const Derived3 &csrc, typename Derived::Scalar epsilon)
{
    static_assert(Derived ::IsVectorAtCompileTime && int(Derived ::SizeAtCompileTime) == 2, "try_circle_center(): 1st point is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "try_circle_center(): 2nd point is not a 2D vector");
    static_assert(Derived3::IsVectorAtCompileTime && int(Derived3::SizeAtCompileTime) == 2, "try_circle_center(): 3rd point is not a 2D vector");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value && std::is_same<typename Derived::Scalar, typename Derived3::Scalar>::value, 
        "try_circle_center(): All three points must be of the same type.");
    using Scalar = typename Derived::Scalar;
    using Vector = Eigen::Matrix<Scalar, 2, 1, Eigen::DontAlign>;
    Vector b  = bsrc - a;
    Vector c  = csrc - a;
    Scalar lb = b.squaredNorm();
    Scalar lc = c.squaredNorm();
    if (Scalar d = b.x() * c.y() - b.y() * c.x(); std::abs(d) < epsilon) {
        // The three points are collinear.
        return {};
    } else {
        Vector v = lc * b - lb * c;
        return std::make_optional<Vector>(a + Vector(- v.y(), v.x()) / (2 * d));
    }
}

// 2D circle defined by its center and squared radius
template<typename Vector>
struct CircleSq {
    using Scalar = typename Vector::Scalar;

    Vector center;
    Scalar radius2;

    CircleSq() {}
    CircleSq(const Vector &center, const Scalar radius2) : center(center), radius2(radius2) {}
    CircleSq(const Vector &a, const Vector &b) : center(Scalar(0.5) * (a + b)) { radius2 = (a - center).squaredNorm(); }
    CircleSq(const Vector &a, const Vector &b, const Vector &c, Scalar epsilon) {
        this->center = circle_center(a, b, c, epsilon);
		this->radius2 = (a - this->center).squaredNorm();
    }

    bool invalid() const { return this->radius2 < 0; }
    bool valid() const { return ! this->invalid(); }
    bool contains(const Vector &p) const { return (p - this->center).squaredNorm() < this->radius2; }
    bool contains(const Vector &p, const Scalar epsilon2) const { return (p - this->center).squaredNorm() < this->radius2 + epsilon2; }

    CircleSq inflated(Scalar epsilon) const 
    	{ assert(this->radius2 >= 0); Scalar r = sqrt(this->radius2) + epsilon; return { this->center, r * r }; }

    static CircleSq make_invalid() { return CircleSq { { 0, 0 }, -1 }; }
};

// 2D circle defined by its center and radius
template<typename Vector>
struct Circle {
    using Scalar = typename Vector::Scalar;

    Vector center;
    Scalar radius;

    Circle() = default;
    Circle(const Vector &center, const Scalar radius) : center(center), radius(radius) {}
    Circle(const Vector &a, const Vector &b) : center(Scalar(0.5) * (a + b)) { radius = (a - center).norm(); }
    Circle(const Vector &a, const Vector &b, const Vector &c, const Scalar epsilon) { *this = CircleSq(a, b, c, epsilon); }

    // Conversion from CircleSq
    template<typename Vector2>
    explicit Circle(const CircleSq<Vector2> &c) : center(c.center), radius(c.radius2 <= 0 ? c.radius2 : sqrt(c.radius2)) {}
    template<typename Vector2>
    Circle operator=(const CircleSq<Vector2>& c) { this->center = c.center; this->radius = c.radius2 <= 0 ? c.radius2 : sqrt(c.radius2); }

    bool invalid() const { return this->radius < 0; }
    bool valid() const { return ! this->invalid(); }
    bool contains(const Vector &p) const { return (p - this->center).squaredNorm() <= this->radius * this->radius; }
    bool contains(const Vector &p, const Scalar epsilon) const
    	{ Scalar re = this->radius + epsilon; return (p - this->center).squaredNorm() < re * re; }

    Circle inflated(Scalar epsilon) const { assert(this->radius >= 0); return { this->center, this->radius + epsilon }; }

    static Circle make_invalid() { return Circle { { 0, 0 }, -1 }; }
};

using Circlef = Circle<Vec2f>;
using Circled = Circle<Vec2d>;
using CircleSqf = CircleSq<Vec2f>;
using CircleSqd = CircleSq<Vec2d>;

/// Find the center of the circle corresponding to the vector of Points as an arc.
Point circle_center_taubin_newton(const Points::const_iterator& input_start, const Points::const_iterator& input_end, size_t cycles = 20);
inline Point circle_center_taubin_newton(const Points& input, size_t cycles = 20) { return circle_center_taubin_newton(input.cbegin(), input.cend(), cycles); }

/// Find the center of the circle corresponding to the vector of Pointfs as an arc.
Vec2d circle_center_taubin_newton(const Vec2ds::const_iterator& input_start, const Vec2ds::const_iterator& input_end, size_t cycles = 20);
inline Vec2d circle_center_taubin_newton(const Vec2ds& input, size_t cycles = 20) { return circle_center_taubin_newton(input.cbegin(), input.cend(), cycles); }
Circled circle_taubin_newton(const Vec2ds& input, size_t cycles = 20);

// Find circle using RANSAC randomized algorithm.
Circled circle_ransac(const Vec2ds& input, size_t iterations = 20, double* min_error = nullptr);

// Linear Least squares fitting.
// Be careful! The linear least squares fitting is strongly biased towards small circles,
// thus the method is only recommended for circles or arches with large arc angle.
// Also it is strongly recommended to center the input at an expected circle (or arc) center
// to minimize the small circle bias!
    // Linear Least squares fitting with SVD. Most accurate, but slowest.
    Circled circle_linear_least_squares_svd(const Vec2ds &input);
    // Linear Least squares fitting with QR decomposition. Medium accuracy, medium speed.
    Circled circle_linear_least_squares_qr(const Vec2ds &input);
    // Linear Least squares fitting solving normal equations. Low accuracy, high speed.
    Circled circle_linear_least_squares_normal(const Vec2ds &input);

// Randomized algorithm by Emo Welzl, working with squared radii for efficiency. The returned circle radius is inflated by epsilon.
template<typename Vector, typename Points>
CircleSq<Vector> smallest_enclosing_circle2_welzl(const Points &points, const typename Vector::Scalar epsilon)
{
    using Scalar = typename Vector::Scalar;
    CircleSq<Vector> circle;

    if (! points.empty()) {
	    const auto &p0 = points[0].template cast<Scalar>();
	    if (points.size() == 1) {
	    	circle.center = p0;
	    	circle.radius2 = epsilon * epsilon;
	    } else {
		    circle = CircleSq<Vector>(p0, points[1].template cast<Scalar>()).inflated(epsilon);
		    for (size_t i = 2; i < points.size(); ++ i)
		        if (const Vector &p = points[i].template cast<Scalar>(); ! circle.contains(p)) {
		            // p is the first point on the smallest circle enclosing points[0..i]
		            circle = CircleSq<Vector>(p0, p).inflated(epsilon);
		            for (size_t j = 1; j < i; ++ j)
		                if (const Vector &q = points[j].template cast<Scalar>(); ! circle.contains(q)) {
		                    // q is the second point on the smallest circle enclosing points[0..i]
		                    circle = CircleSq<Vector>(p, q).inflated(epsilon);
		                    for (size_t k = 0; k < j; ++ k)
		                        if (const Vector &r = points[k].template cast<Scalar>(); ! circle.contains(r))
                                    circle = CircleSq<Vector>(p, q, r, epsilon).inflated(epsilon);
		                }
		        }
		}
	}

    return circle;
}

// Randomized algorithm by Emo Welzl. The returned circle radius is inflated by epsilon.
template<typename Vector, typename Points>
Circle<Vector> smallest_enclosing_circle_welzl(const Points &points, const typename Vector::Scalar epsilon)
{
    return Circle<Vector>(smallest_enclosing_circle2_welzl<Vector, Points>(points, epsilon));
}

// Randomized algorithm by Emo Welzl. The returned circle radius is inflated by SCALED_EPSILON.
inline Circled smallest_enclosing_circle_welzl(const Points &points)
{
    return smallest_enclosing_circle_welzl<Vec2d, Points>(points, SCALED_EPSILON);
}

// Ugly named variant, that accepts the squared line 
// Don't call me with a nearly zero length vector!
// sympy: 
// factor(solve([a * x + b * y + c, x**2 + y**2 - r**2], [x, y])[0])
// factor(solve([a * x + b * y + c, x**2 + y**2 - r**2], [x, y])[1])
template<typename T>
int ray_circle_intersections_r2_lv2_c(T r2, T a, T b, T lv2, T c, std::pair<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>, Eigen::Matrix<T, 2, 1, Eigen::DontAlign>> &out)
{
    T x0 = - a * c;
    T y0 = - b * c;
    T d2 = r2 * lv2 - c * c;
    if (d2 < T(0))
        return 0;
    T d = sqrt(d2);
    out.first.x() = (x0 + b * d) / lv2;
    out.first.y() = (y0 - a * d) / lv2;
    out.second.x() = (x0 - b * d) / lv2;
    out.second.y() = (y0 + a * d) / lv2;
    return d == T(0) ? 1 : 2;
}
template<typename T>
int ray_circle_intersections(T r, T a, T b, T c, std::pair<Eigen::Matrix<T, 2, 1, Eigen::DontAlign>, Eigen::Matrix<T, 2, 1, Eigen::DontAlign>> &out)
{
    T lv2 = a * a + b * b;
    if (lv2 < T(SCALED_EPSILON * SCALED_EPSILON)) {
        //FIXME what is the correct epsilon?
        // What if the line touches the circle?
        return false;
    }
    return ray_circle_intersections_r2_lv2_c2(r * r, a, b, a * a + b * b, c, out);
}

} } // namespace Slic3r::Geometry

#endif // slic3r_Geometry_Circle_hpp_
