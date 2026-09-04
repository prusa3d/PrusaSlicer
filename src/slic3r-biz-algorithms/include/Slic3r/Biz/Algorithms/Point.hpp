#pragma once

#include "Slic3r/Domain/Point.hpp"

namespace Slic3r::Biz::Algorithms::Point {

template <Domain::UnscaledVector Vec>
Vec round(const Vec& vector) {
    return {std::round(vector.x()), std::round(vector.y())};
}

/**
 * Checks if the Points contains consecutive duplicate points.
 *
 * @param points Points to search within.
 * @return true If at least one pair of consecutive duplicate points is found.
 * @return false Otherwise.
 */
bool has_consecutive_duplicate_points(const Domain::Points& points);

/**
 * Removes consecutive duplicate points from the Points.
 *
 * @param points Reference to Points to process and modify in-place.
 * @param check_first_and_last Indicate whether to check for a duplicate between the first and last point.
 * @return true If at least one duplicate point was removed.
 * @return false If no duplicates were found.
 */
bool remove_consecutive_duplicate_points(Domain::Points& points, bool check_first_and_last = false);

Domain::Points scaled(const Domain::Vec2ds& points);
Domain::Vec2ds unscaled(const Domain::Points& points);

template<typename Derived>
Domain::Advanced::Vec<typename Derived::Scalar, 2> to_2d(const Eigen::MatrixBase<Derived> &ptN) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) >= 3, "to_2d(): first parameter is not a 3D or higher dimensional vector");
    return ptN.template head<2>();
}

template<typename Derived>
inline Domain::Advanced::Vec<typename Derived::Scalar, 3> to_3d(const Eigen::MatrixBase<Derived> &pt, const typename Derived::Scalar z) {
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "to_3d(): first parameter is not a 2D vector");
    return { pt.x(), pt.y(), z };
}

Domain::Points collect_duplicates(Domain::Points pts /* Copy */);

} // namespace Slic3r::Biz::Algorithms::Point
