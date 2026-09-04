#pragma once

#include "Slic3r/Domain/Types.hpp"

#include <cmath>
#include <optional>

namespace Slic3r::Domain {

// FIXME This epsilon value is used for many non-related purposes:
// For a threshold of a squared Euclidean distance,
// for a trheshold in a difference of radians,
// for a threshold of a cross product of two non-normalized vectors etc.
static constexpr double EPSILON = 1e-4;

// TODO: this probably should not be here!
template <typename Number>
constexpr inline bool is_approx(Number value, Number test_value, Number precision = EPSILON)
{
    return std::fabs(double(value) - double(test_value)) < double(precision);
}

template <typename Number>
constexpr inline bool is_approx(
    const std::optional<Number>& value,
    const std::optional<Number>& test_value,
    Number precision = EPSILON
)
{
    return value.has_value() && test_value.has_value() && is_approx(*value, *test_value);
}

inline bool is_approx(const Vec3d& p1, const Vec3d& p2, double epsilon = EPSILON)
{
    Vec3d d = (p2 - p1).cwiseAbs();
    return d.x() < epsilon && d.y() < epsilon && d.z() < epsilon;
}

} // namespace Slic3r::Domain
