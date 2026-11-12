#pragma once

#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Arrange/Bed.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

// This is a kernel wrapper that will apply a penality to the object function
// if the result cannot fit into the given rectangular bounds. This can be used
// to arrange into rectangular boundaries without calculating the IFP of the
// rectangle bed. Note that after the arrangement, what is garanteed is that
// the resulting pile will fit into the rectangular boundaries, but it will not
// be within the given rectangle. The items need to be moved afterwards manually.
// Use RectangeOverfitPackingStrategy to automate this post process step.
struct RectangleOverfitKernelWrapper : public IKernel
{
    std::unique_ptr<IKernel> k;
    Domain::BoundingBox2crd binbb;
    Domain::BoundingBox2crd pilebb;

    RectangleOverfitKernelWrapper(std::unique_ptr<IKernel> kern, const Domain::BoundingBox2crd& limits);

    double overfit(const Domain::BoundingBox2crd& itmbb) const;

    double placement_fitness(const ArrangeItem& item, const Domain::Vec2crd& transl) const final;

    bool on_start_packing(
        ArrangeItem& itm,
        const IBed& bed,
        const PackingContext& packing_context,
        std::span<const ArrangeItem> remaining_items
    ) final;

    bool on_item_packed(ArrangeItem& itm) final;
};

} // namespace Slic3r::Biz::Arrange::Kernels
