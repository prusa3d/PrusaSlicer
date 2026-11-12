#pragma once

#include <span>
#include "Slic3r/Biz/Arrange/Bed.hpp"
#include "Slic3r/Biz/Arrange/PackingContext.hpp"
#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"

namespace Slic3r::Biz::Arrange::Kernels {
struct IKernel
{
    virtual ~IKernel() = default;

    virtual double placement_fitness(const ArrangeItem& itm, const Domain::Vec2crd& transl) const = 0;

    virtual bool on_start_packing(
        ArrangeItem& itm,
        const IBed& bed,
        const PackingContext& packing_context,
        std::span<const ArrangeItem> remaining_items
    ) = 0;

    virtual bool on_item_packed(ArrangeItem& itm) = 0;
};
} // namespace Slic3r::Biz::Arrange::Kernels
