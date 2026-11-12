#pragma once

#include <Eigen/Dense>
#include <numbers>

namespace Slic3r {

/**
 * Cross product of two 2D vectors.
 *
 * @param v1 First vector.
 * @param v2 Second vector.
 * @return Cross product of two 2D vectors.
 * @note None of the vectors may be of int32_t type as the result would overflow.
 */
template<typename Derived, typename Derived2>
inline typename Derived::Scalar cross2(const Eigen::MatrixBase<Derived>& v1, const Eigen::MatrixBase<Derived2>& v2)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "cross2(): first parameter is not a 2D vector");
    static_assert(Derived2::IsVectorAtCompileTime && int(Derived2::SizeAtCompileTime) == 2, "cross2(): first parameter is not a 2D vector");
    static_assert(!std::is_same<typename Derived::Scalar, int32_t>::value, "cross2(): Scalar type must not be int32_t, otherwise the cross product would overflow.");
    static_assert(std::is_same<typename Derived::Scalar, typename Derived2::Scalar>::value, "cross2(): Scalar types of 1st and 2nd operand must be equal.");

    return v1.x() * v2.y() - v1.y() * v2.x();
}

// 2D vector perpendicular to the argument.
template<typename Derived>
inline Eigen::Matrix<typename Derived::Scalar, 2, 1, Eigen::DontAlign> perp(const Eigen::MatrixBase<Derived> &v)
{
    static_assert(Derived::IsVectorAtCompileTime && int(Derived::SizeAtCompileTime) == 2, "perp(): parameter is not a 2D vector");
    return { - v.y(), v.x() };
}

template<typename T>
constexpr inline T sqr(T x)
{
    return x * x;
}

template<typename T>
T rad2deg(T angle)
{
    return T(180.0) * angle / std::numbers::pi_v<T>;
}
template<typename T>
constexpr T deg2rad(const T angle)
{
    return std::numbers::pi_v<T> * angle / T(180.0);
}

template<typename T>
T angle_to_0_2PI(T angle)
{
    static const T TWO_PI = T(2) * std::numbers::pi_v<T>;
    while (angle < T(0)) {
        angle += TWO_PI;
    }
    while (TWO_PI < angle) {
        angle -= TWO_PI;
    }

    return angle;
}

template <typename T>
T sign(T val) {
    return T((T(0) < val) - (val < T(0)));
}

template <typename T>
T smoothstep(T val) {
    return val * val * (T(3) - T(2) * val);
}

} // namespace Slic3r
