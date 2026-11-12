#pragma once
#include <Eigen/Dense>

namespace Slic3r::Domain {

/**@typedef coord_t
 * @brief Value 1.0/SCALING_FACTOR corresponds to 1mm. */
using coord_t = int32_t;

namespace Advanced {

template <typename Scalar, int Dim>
using SquareMatrix = Eigen::Matrix<Scalar, Dim, Dim, Eigen::DontAlign>;

template <typename Scalar, int Dim>
class Vec : public Eigen::Matrix<Scalar, Dim, 1, Eigen::DontAlign>
{
    using Parent = Eigen::Matrix<Scalar, Dim, 1, Eigen::DontAlign>;

public:
    // Eigen vectors are not 0 initialized. Fix that.
    Vec() : Parent{Parent::Zero()} {}

    using Parent::Parent;
    using Parent::operator=;

    // These operators address undesired behavior of Eigen (actually c++ in general).
    // Eigen defines operator*(Point, int) which sadly due to c++ implicit
    // conversion can be used with double. It first implicitly narrows the double
    // to int and then does the product. This means that (10, 10) * 1.2 == (10, 10).
    // These operators do the product in doubles and than convert it to int,
    // meaning (10, 10) * 1.2 == (12, 12).
    Vec& operator*=(const double& scalar)
    {
        for (std::size_t i{0}; i < Dim; ++i) {
            (*this)[i] = static_cast<Scalar>((*this)[i] * scalar);
        }
        return *this;
    }

    Vec operator*(const double& scalar) const
    {
        Vec result{*this};
        result *= scalar;
        return result;
    }

    bool operator<(const Vec& other) const
    {
        for (std::size_t i{}; i < Dim; ++i) {
            if ((*this)[i] == other[i]) {
                continue;
            }
            return (*this)[i] < other[i];
        }
        return false;
    }
};

template <typename Scalar, int Dim>
using Transform = Eigen::Transform<Scalar, Dim, Eigen::Affine, Eigen::DontAlign>;

template <typename Scalar, int Dim>
using Translation = Eigen::Translation<Scalar, Dim>;
} // namespace Advanced

/**@typedef Vec2crd
 * @brief Vec2crd{1.0/SCALING_FACTOR, 1.0/SCALING_FACTOR} corresponds to (1mm, 1mm). */
using Vec2crd = Advanced::Vec<coord_t, 2>;

/**@typedef Vec3crd
 * @brief See Vec2crd. */
using Vec3crd = Advanced::Vec<coord_t, 3>;

/**@typedef Vec2big
 * @brief Can hold result of Vec2crd arithmetic operations. */
using Vec2big = Advanced::Vec<int64_t, 2>;

/**@typedef Vec3big
 * @brief See Vec2big */
using Vec3big = Advanced::Vec<int64_t, 3>;

using Vec2f = Advanced::Vec<float, 2>;
using Vec3f = Advanced::Vec<float, 3>;
using Vec4f = Advanced::Vec<float, 4>;
using Vec2d = Advanced::Vec<double, 2>;
using Vec3d = Advanced::Vec<double, 3>;
using Vec4d = Advanced::Vec<double, 4>;

using Vec2fs = std::vector<Domain::Vec2f>;
using Vec2ds = std::vector<Domain::Vec2d>;
using Vec3fs = std::vector<Domain::Vec3f>;
using Vec3ds = std::vector<Domain::Vec3d>;

using Index2 = std::array<int, 2>;
using Index3 = std::array<int, 3>;

using Transform2f = Advanced::Transform<float, 2>;
using Transform2d = Advanced::Transform<double, 2>;
using Transform3f = Advanced::Transform<float, 3>;
using Transform3d = Advanced::Transform<double, 3>;

using SquareMatrix2f = Advanced::SquareMatrix<float, 2>;
using SquareMatrix2d = Advanced::SquareMatrix<double, 2>;
using SquareMatrix3f = Advanced::SquareMatrix<float, 3>;
using SquareMatrix3d = Advanced::SquareMatrix<double, 3>;
using SquareMatrix4f = Advanced::SquareMatrix<float, 4>;
using SquareMatrix4d = Advanced::SquareMatrix<double, 4>;

template <typename T>
concept ScaledScalar = (std::is_same_v<T, coord_t> || std::is_same_v<T, int64_t>);

template <typename T>
concept UnscaledScalar = (std::is_same_v<T, float> || std::is_same_v<T, double>);

template <typename T>
concept ScaledVector = (std::is_same_v<typename T::Scalar, coord_t> || std::is_same_v<typename T::Scalar, int64_t>);

template <typename T>
concept UnscaledVector = (std::is_same_v<typename T::Scalar, float> || std::is_same_v<typename T::Scalar, double>);

/**
 * @brief Compare two numbers if there are (almost) same (with respect to epsilon).
 * @note This function uses both relative (w.r.t @p a and @p b) epsilon and absolute epsilon,
 * taking into account the bigger one. This makes this comparison robust even for numbers close
 * to zero.
 * @return true if two numbers are assumed same, false otherwise
 * @note is not the same as is_approx which only does absolute comparison
 */
template <typename T>
    requires std::floating_point<T>
bool fuzzy_compare(
    T a,
    T b,
    T rel_epsilon = std::numeric_limits<T>::epsilon() * 100,
    T abs_epsilon = std::numeric_limits<T>::epsilon()
)
{
    return std::fabs(a - b)
        <= std::fmax(abs_epsilon, rel_epsilon * std::fmax(std::fabs(a), std::fabs(b)));
}

} // namespace Slic3r::Domain
