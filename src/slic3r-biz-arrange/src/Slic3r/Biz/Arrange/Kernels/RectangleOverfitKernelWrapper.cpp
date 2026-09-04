#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Arrange/Bed.hpp"
#include "Slic3r/Biz/Arrange/Kernels/RectangleOverfitKernelWrapper.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Algorithms::BoundingBox::merge;
using Algorithms::BoundingBox::sizes;
using Algorithms::BoundingBox::translated;
using Domain::SCALED_EPSILON;

// This is a kernel wrapper that will apply a penality to the object function
// if the result cannot fit into the given rectangular bounds. This can be used
// to arrange into rectangular boundaries without calculating the IFP of the
// rectangle bed. Note that after the arrangement, what is garanteed is that
// the resulting pile will fit into the rectangular boundaries, but it will not
// be within the given rectangle. The items need to be moved afterwards manually.
// Use RectangeOverfitPackingStrategy to automate this post process step.

RectangleOverfitKernelWrapper::RectangleOverfitKernelWrapper(
    std::unique_ptr<IKernel> kern,
    const Domain::BoundingBox2crd& limits
) :
    k{std::move(kern)},
    binbb{limits}
{}

double RectangleOverfitKernelWrapper::overfit(const Domain::BoundingBox2crd& itmbb) const
{
    const auto fullbb = merge(pilebb, itmbb);
    auto fullbbsz     = sizes(fullbb);
    auto binbbsz      = sizes(binbb);

    auto wdiff  = fullbbsz.x() - binbbsz.x() - SCALED_EPSILON;
    auto hdiff  = fullbbsz.y() - binbbsz.y() - SCALED_EPSILON;
    double miss = .0;
    if (wdiff > 0)
        miss += double(wdiff);
    if (hdiff > 0)
        miss += double(hdiff);

    miss = miss > 0 ? miss : 0;

    return miss;
}

double RectangleOverfitKernelWrapper::placement_fitness(
    const ArrangeItem& item,
    const Domain::Vec2crd& transl
) const
{
    double score = k->placement_fitness(item, transl);

    const auto itmbb = translated(item.movable_shape().bounding_box(), transl);
    double miss      = overfit(itmbb);
    score -= miss * miss;

    return score;
}

bool RectangleOverfitKernelWrapper::on_start_packing(
    ArrangeItem& itm,
    const IBed& bed,
    const PackingContext& packing_context,
    std::span<const ArrangeItem> remaining_items
)
{
    pilebb = Domain::BoundingBox2crd{};

    for (auto& fitm : packing_context.all_items()) {
        pilebb = merge(pilebb, fitm.fixed_shape().bounding_box());
    }

    return k->on_start_packing(itm, RectangleBed{binbb}, packing_context, remaining_items);
}

bool RectangleOverfitKernelWrapper::on_item_packed(ArrangeItem& itm)
{
    bool ret = k->on_item_packed(itm);

    double miss = overfit(itm.movable_shape().bounding_box());

    if (miss > 0.)
        ret = false;

    return ret;
}
} // namespace Slic3r::Biz::Arrange::Kernels
