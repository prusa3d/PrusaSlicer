#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Domain {

namespace Impl {
template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] bool is_equal(const BoxType& a, const BoxType& b) {
    ASSERT(a.defined && b.defined);
    return a.min == b.min && a.max == b.max;
}
}

[[nodiscard]] bool operator==(const BoundingBox2crd& a, const BoundingBox2crd& b) {
    return Impl::is_equal(a, b);
}

[[nodiscard]] bool operator==(const BoundingBox3crd& a, const BoundingBox3crd& b) {
    return Impl::is_equal(a, b);
}

template <typename _Scalar, std::size_t _Dim>
[[nodiscard]] bool BoundingBox<_Scalar, _Dim>::contains(
    const typename BoundingBox<_Scalar, _Dim>::VecType& point
) const
{
    return (point.array() >= min.array()).all() && (point.array() <= max.array()).all();
}
template bool BoundingBox<coord_t, 2>::contains(const Vec2crd&) const;
template bool BoundingBox<coord_t, 3>::contains(const Vec3crd&) const;
template bool BoundingBox<float, 2>::contains(const Vec2f&) const;
template bool BoundingBox<float, 3>::contains(const Vec3f&) const;
template bool BoundingBox<double, 2>::contains(const Vec2d&) const;
template bool BoundingBox<double, 3>::contains(const Vec3d&) const;

template <typename _Scalar, std::size_t _Dim>
[[nodiscard]] bool BoundingBox<_Scalar, _Dim>::contains(
    const BoundingBox<_Scalar, _Dim>& box
) const
{
    return contains(box.min) && contains(box.max);
}
template bool BoundingBox<coord_t, 2>::contains(const BoundingBox<coord_t, 2> & other) const;
template bool BoundingBox<coord_t, 3>::contains(const BoundingBox<coord_t, 3> & other) const;
template bool BoundingBox<float, 2>::contains(const BoundingBox<float, 2> & other) const;
template bool BoundingBox<float, 3>::contains(const BoundingBox<float, 3> & other) const;
template bool BoundingBox<double, 2>::contains(const BoundingBox<double, 2> & other) const;
template bool BoundingBox<double, 3>::contains(const BoundingBox<double, 3> & other) const;

template <typename _Scalar, std::size_t _Dim>
[[nodiscard]] bool BoundingBox<_Scalar, _Dim>::overlap(
    const BoundingBox<_Scalar, _Dim> & other
) const
{
    return !((max.array() < other.min.array()).any() || (min.array() > other.max.array()).any());
}
template bool BoundingBox<coord_t, 2>::overlap(const BoundingBox<coord_t, 2> & other) const;
template bool BoundingBox<coord_t, 3>::overlap(const BoundingBox<coord_t, 3> & other) const;
template bool BoundingBox<float, 2>::overlap(const BoundingBox<float, 2> & other) const;
template bool BoundingBox<float, 3>::overlap(const BoundingBox<float, 3> & other) const;
template bool BoundingBox<double, 2>::overlap(const BoundingBox<double, 2> & other) const;
template bool BoundingBox<double, 3>::overlap(const BoundingBox<double, 3> & other) const;

}
