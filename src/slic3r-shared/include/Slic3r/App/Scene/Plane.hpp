#pragma once

#include "Slic3r/Domain/Point.hpp"

namespace Slic3r::App::Scene {
struct Ray;

/**
 * @brief An infinite 3D plane
 */
struct Plane
{

    /**
     * @brief "Normal" vector (may not be normalized)
     *
     * In the plane equation: `ax + by + cz + d = 0` the normal is vector of `(a, b, c)`.
     */
    Domain::Vec3d normal;

    /**
     * @brief The `d` coeficient as used in plane equation `ax + by + cz + d = 0.`
     */
    double d;

    /**
     * @brief Normalize the normal of this plane and update d accordingly.
     */
    void normalize();

    /**
      * @brief Return a normalized copy of this plane
      * @return New plane obtained by normalizing this plane
      */
    Plane normalized() const;

    /**
     * @brief Creates 3D plane defined by point on the plane and two (non-parallel) vectors in the plane.
     * @param point Point lying on plane
     * @param v0 Vector in one plane direction
     * @param v1 Vector in other plane direction (have to non-parallel to vector @p v0)
     * @return New plane where @p point lays on the plane and @p v0 and @p v1 are vectors
     * also laying on the plane (relatively to @p point).
     */
    static Plane from_point_and_vectors(const Domain::Vec3d& point, const Domain::Vec3d& v0, const Domain::Vec3d& v1);

    /**
     * @brief Create plane defined by three points laying on it.
     * @note @p p0, @p p1 and @p p2 must NOT be collinear (i.e. all three laying on same line).
     * @param p0 First plane point
     * @param p1 Second plane point
     * @param p2 Third plane point
     * @return Plane containing all three points.
     */
    static Plane from_three_points(const Domain::Vec3d& p0, const Domain::Vec3d& p1, const Domain::Vec3d& p2)
    { return from_point_and_vectors(p0, p1 - p0, p2 - p0); }

    /**
     * @brief Creates 3D plane passing throught the given point and having the given normal.
     * @param p Point lying on plane
     * @param v normal to the plane
     * @return New plane where @p point lays on the plane and @n is the normal.
     */
    static Plane from_point_and_normal(const Domain::Vec3d& p, const Domain::Vec3d& n)
    { return { n, -n.dot(p) }; }

    /**
     * @brief Tests if @p ray intersects this plane.
     * @param[in] ray Ray definition
     * @param[out] t If `true` returned, @p t will be filled with `t` parameter, which plugged into
     * ray equation (`origin + direction * t`) will get you intersection point.
     * @return `True` if intersection of this plane and @p ray exists, otherwise `false`.
     */
    bool intersects(const Ray& ray, double& t) const;

    /**
      * @brief Tests if @box box intersects this plane.
      * @param[in] box Axis aligned box to test
      * @return `True` if this plane interstects @box box, otherwise `false`.
      */
    bool intersects(const Eigen::AlignedBox3d& box) const;

    /**
     * @brief Tests if the sphere defined by @center center and @radius radius intersects this plane.
     * @param[in] center Center of the sphere to test
     * @param[in] radius Radius of the sphere to test
     * @return `True` if this plane interstects the sphere, otherwise `false`.
     */
    bool intersects(const Domain::Vec3d& center, double radius) const
    { return distance(center) < radius; }

    /**
      * @brief Calculate signed distance between @p point and this plane.
      * @param[in] p Point
      * @return The signed distance between @p point and this plane.
      */
    double signed_distance(const Domain::Vec3d& p) const
    { return normal.dot(p) + d; }

    /**
     * @brief Calculate distance between @p point and this plane.
     * @param[in] p Point
     * @return The distance between @p point and this plane.
     */
    double distance(const Domain::Vec3d& p) const
    { return std::abs(signed_distance(p)); }

    /**
     * @brief Get two perpendicular vectors in the plane.
     * @param[out] u
     * @param[out] v
     */
    void vectors_in_plane(Domain::Vec3d& u, Domain::Vec3d& v) const
    {
        size_t max_axis = 5;
        normal.maxCoeff(&max_axis);

        Domain::Vec3d v1 = max_axis == 0 ? Domain::Vec3d{0, 1, 0} : Domain::Vec3d{1, 0, 0};
        u = (v1 - v1.dot(normal) * normal).normalized();
        v = u.cross(normal).normalized();
    }
};
} // namespace Slic3r::App::Scene
