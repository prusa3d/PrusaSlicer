#pragma once

#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

struct GravityKernel : public IKernel
{
    std::optional<Domain::Vec2crd> sink;
    std::optional<Domain::Vec2crd> item_sink;
    Domain::Vec2d active_sink;

    GravityKernel(Domain::Vec2crd gravity_center);

    GravityKernel() = default;

    double placement_fitness(const ArrangeItem& itm, const Domain::Vec2crd& transl) const final;

    bool on_start_packing(
        ArrangeItem& itm,
        const IBed& bed,
        const PackingContext& packing_context,
        std::span<const ArrangeItem> /*remaining_items*/
    ) final;

    bool on_item_packed(ArrangeItem& itm) final;
};

} // namespace Slic3r::Biz::Arrange::Kernels
