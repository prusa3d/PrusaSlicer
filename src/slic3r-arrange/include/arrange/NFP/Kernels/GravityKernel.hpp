///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef GRAVITYKERNEL_HPP
#define GRAVITYKERNEL_HPP

#include <arrange/NFP/NFPArrangeItemTraits.hpp>
#include <arrange/Beds.hpp>

#include "KernelUtils.hpp"

namespace Slic3r { namespace arr2 {

struct GravityKernel {
    std::optional<Vec2crd> sink;
    std::optional<Vec2crd> item_sink;
    Vec2d active_sink;

    GravityKernel(Vec2crd gravity_center) :
        sink{gravity_center}, active_sink{unscaled(gravity_center)} {}

    GravityKernel() = default;

    template<class ArrItem>
    double placement_fitness(const ArrItem &itm, const Vec2crd &transl) const
    {
        Vec2d center = unscaled(envelope_centroid(itm));

        center += unscaled(transl);

        return - (center - active_sink).squaredNorm();
    }

    template<class ArrItem, class Bed, class Ctx, class RemIt>
    bool on_start_packing(ArrItem &itm,
                          const Bed &bed,
                          const Ctx &packing_context,
                          const Range<RemIt> & /*remaining_items*/)
    {
        bool ret = false;

        item_sink = get_gravity_sink(itm);

        if (!sink) {
            sink = bounding_box(bed).center();
        }

        if (item_sink)
            active_sink = unscaled(*item_sink);
        else
            active_sink = unscaled(*sink);

        ret = find_initial_position(itm, scaled(active_sink), bed, packing_context);

        return ret;
    }

    template<class ArrItem> bool on_item_packed(ArrItem &itm) { return true; }
};

}} // namespace Slic3r::arr2

#endif // GRAVITYKERNEL_HPP
