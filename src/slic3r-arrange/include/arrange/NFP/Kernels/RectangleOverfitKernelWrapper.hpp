///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef RECTANGLEOVERFITKERNELWRAPPER_HPP
#define RECTANGLEOVERFITKERNELWRAPPER_HPP

#include "KernelTraits.hpp"

#include <arrange/NFP/NFPArrangeItemTraits.hpp>
#include <arrange/Beds.hpp>

namespace Slic3r { namespace arr2 {

// This is a kernel wrapper that will apply a penality to the object function
// if the result cannot fit into the given rectangular bounds. This can be used
// to arrange into rectangular boundaries without calculating the IFP of the
// rectangle bed. Note that after the arrangement, what is garanteed is that
// the resulting pile will fit into the rectangular boundaries, but it will not
// be within the given rectangle. The items need to be moved afterwards manually.
// Use RectangeOverfitPackingStrategy to automate this post process step.
template<class Kernel>
struct RectangleOverfitKernelWrapper {
    Kernel &k;
    BoundingBox binbb;
    BoundingBox pilebb;

    RectangleOverfitKernelWrapper(Kernel &kern, const BoundingBox &limits)
        : k{kern}
          , binbb{limits}
    {}

    double overfit(const BoundingBox &itmbb) const
    {
        auto fullbb = pilebb;
        fullbb.merge(itmbb);
        auto fullbbsz = fullbb.size();
        auto binbbsz  = binbb.size();

        auto wdiff = fullbbsz.x() - binbbsz.x() - SCALED_EPSILON;
        auto hdiff = fullbbsz.y() - binbbsz.y() - SCALED_EPSILON;
        double miss = .0;
        if (wdiff > 0)
            miss += double(wdiff);
        if (hdiff > 0)
            miss += double(hdiff);

        miss = miss > 0? miss : 0;

        return miss;
    }

    template<class ArrItem>
    double placement_fitness(const ArrItem &item, const Vec2crd &transl) const
    {
        double score = KernelTraits<Kernel>::placement_fitness(k, item, transl);

        auto itmbb = envelope_bounding_box(item);
        itmbb.translate(transl);
        double miss = overfit(itmbb);
        score -= miss * miss;

        return score;
    }

    template<class ArrItem, class Bed, class Ctx, class RemIt>
    bool on_start_packing(ArrItem &itm,
                          const Bed &bed,
                          const Ctx &packing_context,
                          const Range<RemIt> &remaining_items)
    {
        pilebb = BoundingBox{};

        for (auto &fitm : all_items_range(packing_context))
            pilebb.merge(fixed_bounding_box(fitm));

        return KernelTraits<Kernel>::on_start_packing(k, itm, RectangleBed{binbb, Vec2crd::Zero()},
                                                      packing_context,
                                                      remaining_items);
    }

    template<class ArrItem>
    bool on_item_packed(ArrItem &itm)
    {
        bool ret = KernelTraits<Kernel>::on_item_packed(k, itm);

        double miss = overfit(envelope_bounding_box(itm));

        if (miss > 0.)
            ret = false;

        return ret;
    }
};

}} // namespace Slic3r::arr2

#endif // RECTANGLEOVERFITKERNELWRAPPER_H
