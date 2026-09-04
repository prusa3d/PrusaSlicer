#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Algorithms::BoundingBox {

[[nodiscard]] auto construct(const std::input_iterator auto from, const std::input_iterator auto to)
{
    using PointType = std::remove_reference_t<decltype(*from)>;
    using ResultType = Domain::BoundingBox<
        typename PointType::Scalar,
        PointType::RowsAtCompileTime
    >;

    if (from == to) {
        return ResultType{};
    }

    auto it{from};
    auto min{*it};
    auto max{*it};
    for (++it; it != to; ++it) {
        min = min.cwiseMin(*it);
        max = max.cwiseMax(*it);
    }

    return ResultType{min, max};
}

[[nodiscard]] auto construct(const std::ranges::range auto& points)
{
    return construct(std::begin(points), std::end(points));
}


namespace Impl {
template <typename T>
constexpr bool is_floating = std::is_same_v<typename T::Scalar, float> || std::is_same_v<typename T::Scalar, double>;
}

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] std::enable_if_t<Impl::is_floating<BoxType>, bool> approx_equals(
    const BoxType& a, const BoxType& b
);

[[nodiscard]] bool empty(const Domain::BoundingBoxConcept auto& box);

/** @brief Zero if the point is inside the box */
template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] double distance(const BoxType &box, const typename BoxType::VecType &point);

/** @brief Zero if the point is inside the box */
template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] double distance_squared(const BoxType &box, const typename BoxType::VecType &point);

/** @brief Zero if the boxes overlap */
template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] double distance(const BoxType &a, const BoxType &b);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType merge(const BoxType& a, const BoxType& b);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType merge(const BoxType& a, const typename BoxType::VecType& point);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] typename BoxType::VecType sizes(const BoxType& box);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType inflated(
    const BoxType& box, const typename BoxType::Scalar& delta
);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] typename BoxType::VecType center(const BoxType& box);

template<Domain::ScaledScalar OutputScalarType = Domain::coord_t, Domain::BoundingBoxConcept InputBoxType>
[[nodiscard]] Domain::BoundingBox<OutputScalarType, InputBoxType::Dim> scaled(const InputBoxType& box);

template<typename OutputScalarType, Domain::BoundingBoxConcept InputBoxType>
[[nodiscard]] Domain::BoundingBox<OutputScalarType, InputBoxType::Dim> cast(const InputBoxType& box);

template<typename OutputScalarType = float, Domain::BoundingBoxConcept InputBoxType>
[[nodiscard]] Domain::BoundingBox<OutputScalarType, InputBoxType::Dim> unscaled(const InputBoxType& box);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] Domain::BoundingBox<typename BoxType::Scalar, 2> to_2d(const BoxType& box);

template<Domain::BoundingBoxConcept BoxType>
[[nodiscard]] BoxType translated(
    const BoxType& box,
    const Domain::Advanced::Vec<typename BoxType::Scalar, BoxType::Dim>& delta
);

Domain::BoundingBox3d transformed(const Domain::BoundingBox3d& box, const Domain::Transform3d& matrix);

inline Domain::Polygon to_polygon(const Domain::BoundingBox2crd& box)
{
    return Domain::Polygon{{
        box.min,
        { box.max.x(), box.min.y() },
        box.max,
        { box.min.x(), box.max.y() }
    }};
}

inline Domain::Vec2ds to_polygon(const Domain::BoundingBox2d& box)
{
    return {
        box.min,
        { box.max.x(), box.min.y() },
        box.max,
        { box.min.x(), box.max.y() }
    };
}

} // namespace Slic3r::Biz::Algorithms::BoundingBox
