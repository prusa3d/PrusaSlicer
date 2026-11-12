#pragma once

#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Algorithms::BoundingBox::center;
using Algorithms::Polygon::to_expolygons;

struct CompactifyKernel : public IKernel
{
    Domain::ExPolygons merged_pile;

    double placement_fitness(const ArrangeItem& itm, const Domain::Vec2crd& transl) const final;

    bool on_start_packing(
        ArrangeItem& itm,
        const IBed& bed,
        const PackingContext& packing_context,
        const std::span<const ArrangeItem> /*remaining_items*/
    ) final;

    bool on_item_packed(ArrangeItem& itm) final;
};

} // namespace Slic3r::Biz::Arrange::Kernels
