///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef COMPACTIFYKERNEL_HPP
#define COMPACTIFYKERNEL_HPP

#include <numeric>

#include "libslic3r/Arrange/Core/NFP/NFPArrangeItemTraits.hpp"
#include "libslic3r/Arrange/Core/Beds.hpp"

#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/ClipperUtils.hpp>

#include "KernelUtils.hpp"

namespace Slic3r { namespace arr2 {

struct CompactifyKernel {
    ExPolygons merged_pile;

    template<class ArrItem>
    double placement_fitness(const ArrItem &itm, const Vec2crd &transl) const
    {
        auto pile = merged_pile;

        ExPolygons itm_tr = to_expolygons(envelope_outline(itm));
        for (auto &p : itm_tr)
            p.translate(transl);

        append(pile, std::move(itm_tr));

        pile = union_ex(pile);

        Polygon chull = Geometry::convex_hull(pile);

        return -(chull.area());
    }

    template<class ArrItem, class Bed, class Context, class RemIt>
    bool on_start_packing(ArrItem &itm,
                          const Bed &bed,
                          const Context &packing_context,
                          const Range<RemIt> & /*remaining_items*/)
    {
        bool ret = find_initial_position(itm, bounding_box(bed).center(), bed,
                                         packing_context);

        merged_pile.clear();
        for (const auto &gitm : all_items_range(packing_context)) {
            append(merged_pile, to_expolygons(fixed_outline(gitm)));
        }
        merged_pile = union_ex(merged_pile);

        return ret;
    }

    template<class ArrItem>
    bool on_item_packed(ArrItem &itm) { return true; }
};

}} // namespace Slic3r::arr2

#endif // COMPACTIFYKERNEL_HPP
