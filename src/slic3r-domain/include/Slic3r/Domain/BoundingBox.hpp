#pragma once

#include "Slic3r/Domain/Types.hpp"
#include <vector>

namespace Slic3r::Domain {

template <typename _Scalar, std::size_t _Dim>
struct BoundingBox {

    using Scalar = _Scalar;
    static constexpr std::size_t Dim = _Dim;
    using VecType = Advanced::Vec<Scalar, Dim>;

    BoundingBox(): min{VecType::Zero()}, max{VecType::Zero()}, defined{false} {}

    BoundingBox(const VecType &min, const VecType &max) :
        min{min}, max{max}, defined{(min.array() < max.array()).all()} {}

    BoundingBox(const VecType &min, const VecType &max, const bool defined) :
        min{min}, max{max}, defined{defined} {}

    VecType min;
    VecType max;
    bool defined{false};

    [[nodiscard]] bool contains(const VecType& point) const;

    [[nodiscard]] bool contains(const BoundingBox& box) const;

    [[nodiscard]] bool overlap(const BoundingBox& other) const;
};

using BoundingBox2crd = BoundingBox<coord_t, 2>;
using BoundingBox3crd = BoundingBox<coord_t, 3>;
using BoundingBox2f = BoundingBox<float, 2>;
using BoundingBox3f = BoundingBox<float, 3>;
using BoundingBox2d = BoundingBox<double, 2>;
using BoundingBox3d = BoundingBox<double, 3>;

using BoundingBoxes2crd = std::vector<BoundingBox2crd>;
using BoundingBoxes3crd = std::vector<BoundingBox3crd>;
using BoundingBoxes2f = std::vector<BoundingBox2f>;
using BoundingBoxes3f = std::vector<BoundingBox3f>;
using BoundingBoxes2d = std::vector<BoundingBox2d>;
using BoundingBoxes3d = std::vector<BoundingBox3d>;

template <typename T>
concept BoundingBoxConcept = requires(T box) {
    typename T::Scalar;
    { T::Dim } -> std::convertible_to<std::size_t>;
    typename T::VecType;

    { box.min } -> std::same_as<typename T::VecType&>;
    { box.max } -> std::same_as<typename T::VecType&>;
    { box.defined } -> std::convertible_to<bool&>;
};

[[nodiscard]] bool operator==(const BoundingBox2crd& a, const BoundingBox2crd& b);
[[nodiscard]] bool operator==(const BoundingBox3crd& a, const BoundingBox3crd& b);

} // namespace Slic3r::Domain

namespace Slic3r {
    // Legacy migration.
    using BoundingBox = Domain::BoundingBox2crd;
    using BoundingBoxf = Domain::BoundingBox2d;
    using BoundingBoxf3 = Domain::BoundingBox3d;
    using  BoundingBoxes = std::vector<Domain::BoundingBox2crd>;
}
