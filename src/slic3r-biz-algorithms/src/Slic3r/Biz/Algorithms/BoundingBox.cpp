#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Assert.hpp"

namespace Slic3r::Biz::Algorithms::BoundingBox {

using Domain::BoundingBox2crd;
using Domain::BoundingBox3crd;
using Domain::BoundingBox2f;
using Domain::BoundingBox3f;
using Domain::BoundingBox2d;
using Domain::BoundingBox3d;

using Domain::Vec2crd;
using Domain::Vec3crd;
using Domain::Vec2f;
using Domain::Vec3f;
using Domain::Vec2d;
using Domain::Vec3d;

using Domain::coord_t;

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] std::enable_if_t<Impl::is_floating<BoxType>, bool> approx_equals(
    const BoxType& a, const BoxType& b
)
{
    ASSERT(a.defined && b.defined);
    return a.min.isApprox(b.min) && a.max.isApprox(b.max);
}
template bool approx_equals<BoundingBox2f>(const BoundingBox2f&, const BoundingBox2f&);
template bool approx_equals<BoundingBox3f>(const BoundingBox3f&, const BoundingBox3f&);
template bool approx_equals<BoundingBox2d>(const BoundingBox2d&, const BoundingBox2d&);
template bool approx_equals<BoundingBox3d>(const BoundingBox3d&, const BoundingBox3d&);

[[nodiscard]] bool empty(const Domain::BoundingBoxConcept auto& box)
{
    return !box.defined || (box.min.array() >= box.max.array()).any();
}
template bool empty<BoundingBox2crd>(const BoundingBox2crd&);
template bool empty<BoundingBox3crd>(const BoundingBox3crd&);
template bool empty<BoundingBox2f>(const BoundingBox2f&);
template bool empty<BoundingBox3f>(const BoundingBox3f&);
template bool empty<BoundingBox2d>(const BoundingBox2d&);
template bool empty<BoundingBox3d>(const BoundingBox3d&);

/** @brief Zero if the point is inside the box */
template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] double distance(const BoxType &box, const typename BoxType::VecType &point) {
    Eigen::AlignedBox<typename BoxType::Scalar, BoxType::Dim> eigen_box{box.min, box.max};
    return eigen_box.exteriorDistance(point);
}
template double distance<BoundingBox2crd>(const BoundingBox2crd&, const Vec2crd&);
template double distance<BoundingBox3crd>(const BoundingBox3crd&, const Vec3crd&);
template double distance<BoundingBox2f>(const BoundingBox2f&, const Vec2f&);
template double distance<BoundingBox3f>(const BoundingBox3f&, const Vec3f&);
template double distance<BoundingBox2d>(const BoundingBox2d&, const Vec2d&);
template double distance<BoundingBox3d>(const BoundingBox3d&, const Vec3d&);

/** @brief Zero if the point is inside the box */
template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] double distance_squared(const BoxType &box, const typename BoxType::VecType &point) {
    const Eigen::AlignedBox<typename BoxType::Scalar, BoxType::Dim> eigen_box{box.min, box.max};
    return eigen_box.squaredExteriorDistance(point);
}
template double distance_squared<BoundingBox2crd>(const BoundingBox2crd&, const Vec2crd&);
template double distance_squared<BoundingBox3crd>(const BoundingBox3crd&, const Vec3crd&);
template double distance_squared<BoundingBox2f>(const BoundingBox2f&, const Vec2f&);
template double distance_squared<BoundingBox3f>(const BoundingBox3f&, const Vec3f&);
template double distance_squared<BoundingBox2d>(const BoundingBox2d&, const Vec2d&);
template double distance_squared<BoundingBox3d>(const BoundingBox3d&, const Vec3d&);

/** @brief Zero if the boxes overlap */
template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] double distance(const BoxType &a, const BoxType &b) {
    using EigenBox = Eigen::AlignedBox<typename BoxType::Scalar, BoxType::Dim>;

    const EigenBox eigen_box_a{a.min, a.max};
    const EigenBox eigen_box_b{b.min, b.max};

    return eigen_box_a.exteriorDistance(eigen_box_b);
}
template double distance<BoundingBox2crd>(const BoundingBox2crd&, const BoundingBox2crd&);
template double distance<BoundingBox3crd>(const BoundingBox3crd&, const BoundingBox3crd&);
template double distance<BoundingBox2f>(const BoundingBox2f&, const BoundingBox2f&);
template double distance<BoundingBox3f>(const BoundingBox3f&, const BoundingBox3f&);
template double distance<BoundingBox2d>(const BoundingBox2d&, const BoundingBox2d&);
template double distance<BoundingBox3d>(const BoundingBox3d&, const BoundingBox3d&);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType merge(const BoxType& a, const BoxType& b) {
    ASSERT(b.defined || (b.min.array() >= b.max.array()).any());

    if (a.defined) {
        return BoxType{a.min.cwiseMin(b.min), a.max.cwiseMax(b.max), true};
    } else {
        BoxType result{b};
        result.defined = true;
        return result;
    }
}
template BoundingBox2crd merge<BoundingBox2crd>(const BoundingBox2crd&, const BoundingBox2crd&);
template BoundingBox3crd merge<BoundingBox3crd>(const BoundingBox3crd&, const BoundingBox3crd&);
template BoundingBox2f merge<BoundingBox2f>(const BoundingBox2f&, const BoundingBox2f&);
template BoundingBox3f merge<BoundingBox3f>(const BoundingBox3f&, const BoundingBox3f&);
template BoundingBox2d merge<BoundingBox2d>(const BoundingBox2d&, const BoundingBox2d&);
template BoundingBox3d merge<BoundingBox3d>(const BoundingBox3d&, const BoundingBox3d&);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType merge(const BoxType& a, const typename BoxType::VecType& point) {
    if (a.defined) {
        return BoxType{a.min.cwiseMin(point), a.max.cwiseMax(point), true};
    } else {
        return BoxType{point, point, true};
    }
}
template BoundingBox2crd merge<BoundingBox2crd>(const BoundingBox2crd&, const Vec2crd&);
template BoundingBox3crd merge<BoundingBox3crd>(const BoundingBox3crd&, const Vec3crd&);
template BoundingBox2f merge<BoundingBox2f>(const BoundingBox2f&, const Vec2f&);
template BoundingBox3f merge<BoundingBox3f>(const BoundingBox3f&, const Vec3f&);
template BoundingBox2d merge<BoundingBox2d>(const BoundingBox2d&, const Vec2d&);
template BoundingBox3d merge<BoundingBox3d>(const BoundingBox3d&, const Vec3d&);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] typename BoxType::VecType sizes(const BoxType& box)
{
    return box.max - box.min;
}
template Vec2crd sizes<BoundingBox2crd>(const BoundingBox2crd&);
template Vec3crd sizes<BoundingBox3crd>(const BoundingBox3crd&);
template Vec2f sizes<BoundingBox2f>(const BoundingBox2f&);
template Vec3f sizes<BoundingBox3f>(const BoundingBox3f&);
template Vec2d sizes<BoundingBox2d>(const BoundingBox2d&);
template Vec3d sizes<BoundingBox3d>(const BoundingBox3d&);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType inflated(
    const BoxType& box, const typename BoxType::Scalar& delta
)
{
    using Vec = typename BoxType::VecType;
    const Vec vector_delta{Vec::Ones() * delta};

    auto result{box};
    result.min -= vector_delta;
    result.max += vector_delta;

    return result;
}
template BoundingBox2crd inflated<BoundingBox2crd>(const BoundingBox2crd&, const coord_t&);
template BoundingBox3crd inflated<BoundingBox3crd>(const BoundingBox3crd&, const coord_t&);
template BoundingBox2f inflated<BoundingBox2f>(const BoundingBox2f&, const float&);
template BoundingBox3f inflated<BoundingBox3f>(const BoundingBox3f&, const float&);
template BoundingBox2d inflated<BoundingBox2d>(const BoundingBox2d&, const double&);
template BoundingBox3d inflated<BoundingBox3d>(const BoundingBox3d&, const double&);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] typename BoxType::VecType center(const BoxType& box)
{
    return (box.min + box.max) / typename BoxType::Scalar{2};
}
template Vec2crd center<BoundingBox2crd>(const BoundingBox2crd&);
template Vec3crd center<BoundingBox3crd>(const BoundingBox3crd&);
template Vec2f center<BoundingBox2f>(const BoundingBox2f&);
template Vec3f center<BoundingBox3f>(const BoundingBox3f&);
template Vec2d center<BoundingBox2d>(const BoundingBox2d&);
template Vec3d center<BoundingBox3d>(const BoundingBox3d&);


template<Domain::ScaledScalar OutputScalarType, Domain::BoundingBoxConcept InputBoxType>
[[nodiscard]] Domain::BoundingBox<OutputScalarType, InputBoxType::Dim> scaled(const InputBoxType& box)
{
    ASSERT(box.defined);

    return {
        Scaling::scaled<OutputScalarType>(box.min),
        Scaling::scaled<OutputScalarType>(box.max)
    };
}
template BoundingBox2crd scaled<coord_t, BoundingBox2f>(const BoundingBox2f&);
template BoundingBox2crd scaled<coord_t, BoundingBox2d>(const BoundingBox2d&);

template<typename OutputScalarType, Domain::BoundingBoxConcept InputBoxType>
[[nodiscard]] Domain::BoundingBox<OutputScalarType, InputBoxType::Dim> cast(const InputBoxType& box)
{
    return {
        box.min.template cast<OutputScalarType>(),
        box.max.template cast<OutputScalarType>()
    };
}
template BoundingBox2d cast<double, BoundingBox2f>(const BoundingBox2f&);
template BoundingBox2f cast<float, BoundingBox2d>(const BoundingBox2d&);
template BoundingBox3d cast<double, BoundingBox3f>(const BoundingBox3f&);
template BoundingBox3f cast<float, BoundingBox3d>(const BoundingBox3d&);

template<typename OutputScalarType, Domain::BoundingBoxConcept InputBoxType>
[[nodiscard]] Domain::BoundingBox<OutputScalarType, InputBoxType::Dim> unscaled(const InputBoxType& box)
{
    ASSERT(box.defined);

    return {
        Scaling::unscaled<OutputScalarType>(box.min),
        Scaling::unscaled<OutputScalarType>(box.max),
    };
}
template BoundingBox2f unscaled<float, BoundingBox2crd>(const BoundingBox2crd&);
template BoundingBox2d unscaled<double, BoundingBox2crd>(const BoundingBox2crd&);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] Domain::BoundingBox<typename BoxType::Scalar, 2> to_2d(const BoxType& box)
{
    return {
        box.min.template head<2>(),
        box.max.template head<2>(),
        box.defined
    };
}
template BoundingBox2crd to_2d<BoundingBox3crd>(const BoundingBox3crd&);
template BoundingBox2f to_2d<BoundingBox3f>(const BoundingBox3f&);
template BoundingBox2d to_2d<BoundingBox3d>(const BoundingBox3d&);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType translated(
    const BoxType& box,
    const Domain::Advanced::Vec<typename BoxType::Scalar, BoxType::Dim>& delta
) {
    ASSERT(box.defined);

    using Domain::Advanced::Transform;
    using Domain::Advanced::Translation;

    const Domain::Advanced::Transform<typename BoxType::Scalar, BoxType::Dim> translation{
        Domain::Advanced::Translation<typename BoxType::Scalar, BoxType::Dim>{delta}};

    return BoxType{translation * box.min, translation * box.max};
}
template BoundingBox2crd translated<BoundingBox2crd>(const BoundingBox2crd&, const Vec2crd&);
template BoundingBox3crd translated<BoundingBox3crd>(const BoundingBox3crd&, const Vec3crd&);
template BoundingBox2f translated<BoundingBox2f>(const BoundingBox2f&, const Vec2f&);
template BoundingBox3f translated<BoundingBox3f>(const BoundingBox3f&, const Vec3f&);
template BoundingBox2d translated<BoundingBox2d>(const BoundingBox2d&, const Vec2d&);
template BoundingBox3d translated<BoundingBox3d>(const BoundingBox3d&, const Vec3d&);

Domain::BoundingBox3d transformed(const Domain::BoundingBox3d& box, const Domain::Transform3d& matrix)
{
    typedef Eigen::Matrix<double, 3, 8, Eigen::DontAlign> Vertices;

    Vertices src_vertices;
    src_vertices(0, 0) = box.min.x(); src_vertices(1, 0) = box.min.y(); src_vertices(2, 0) = box.min.z();
    src_vertices(0, 1) = box.max.x(); src_vertices(1, 1) = box.min.y(); src_vertices(2, 1) = box.min.z();
    src_vertices(0, 2) = box.max.x(); src_vertices(1, 2) = box.max.y(); src_vertices(2, 2) = box.min.z();
    src_vertices(0, 3) = box.min.x(); src_vertices(1, 3) = box.max.y(); src_vertices(2, 3) = box.min.z();
    src_vertices(0, 4) = box.min.x(); src_vertices(1, 4) = box.min.y(); src_vertices(2, 4) = box.max.z();
    src_vertices(0, 5) = box.max.x(); src_vertices(1, 5) = box.min.y(); src_vertices(2, 5) = box.max.z();
    src_vertices(0, 6) = box.max.x(); src_vertices(1, 6) = box.max.y(); src_vertices(2, 6) = box.max.z();
    src_vertices(0, 7) = box.min.x(); src_vertices(1, 7) = box.max.y(); src_vertices(2, 7) = box.max.z();

    Vertices dst_vertices = matrix * src_vertices.colwise().homogeneous();

    Vec3d v_min(dst_vertices(0, 0), dst_vertices(1, 0), dst_vertices(2, 0));
    Vec3d v_max = v_min;

    for (int i = 1; i < 8; ++i) {
        for (int j = 0; j < 3; ++j) {
            v_min(j) = std::min(v_min(j), dst_vertices(j, i));
            v_max(j) = std::max(v_max(j), dst_vertices(j, i));
        }
    }

    return {v_min, v_max};
}

} // namespace Slic3r::Biz::Algorithms::BoundingBox
