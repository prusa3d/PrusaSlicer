///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef NFPARRANGEITEMTRAITS_HPP
#define NFPARRANGEITEMTRAITS_HPP

#include <numeric>

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/BoundingBox.hpp>

#include <arrange/ArrangeBase.hpp>


namespace Slic3r { namespace arr2 {

// Additional methods that an ArrangeItem object has to implement in order
// to be usable with PackStrategyNFP.
template<class ArrItem, class En = void> struct NFPArrangeItemTraits_
{
    template<class Context, class Bed, class StopCond = DefaultStopCondition>
    static ExPolygons calculate_nfp(const ArrItem &item,
                                    const Context &packing_context,
                                    const Bed &bed,
                                    StopCond stop_condition = {})
    {
        static_assert(always_false<ArrItem>::value,
                      "NFP unimplemented for this item type.");
        return {};
    }

    static Vec2crd reference_vertex(const ArrItem &item)
    {
        return item.reference_vertex();
    }

    static BoundingBox envelope_bounding_box(const ArrItem &itm)
    {
        return itm.envelope_bounding_box();
    }

    static BoundingBox fixed_bounding_box(const ArrItem &itm)
    {
        return itm.fixed_bounding_box();
    }

    static const Polygons & envelope_outline(const ArrItem &itm)
    {
        return itm.envelope_outline();
    }

    static const Polygons & fixed_outline(const ArrItem &itm)
    {
        return itm.fixed_outline();
    }

    static const Polygon & envelope_convex_hull(const ArrItem &itm)
    {
        return itm.envelope_convex_hull();
    }

    static const Polygon & fixed_convex_hull(const ArrItem &itm)
    {
        return itm.fixed_convex_hull();
    }

    static double envelope_area(const ArrItem &itm)
    {
        return itm.envelope_area();
    }

    static double fixed_area(const ArrItem &itm)
    {
        return itm.fixed_area();
    }

    static auto allowed_rotations(const ArrItem &)
    {
        return std::array{0.};
    }

    static Vec2crd fixed_centroid(const ArrItem &itm)
    {
        return fixed_bounding_box(itm).center();
    }

    static Vec2crd envelope_centroid(const ArrItem &itm)
    {
        return envelope_bounding_box(itm).center();
    }
};

template<class T>
using NFPArrangeItemTraits = NFPArrangeItemTraits_<StripCVRef<T>>;

template<class ArrItem,
         class Context,
         class Bed,
         class StopCond = DefaultStopCondition>
ExPolygons calculate_nfp(const ArrItem &itm,
                         const Context &context,
                         const Bed &bed,
                         StopCond stopcond = {})
{
    return NFPArrangeItemTraits<ArrItem>::calculate_nfp(itm, context, bed,
                                                        std::move(stopcond));
}

template<class ArrItem> Vec2crd reference_vertex(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::reference_vertex(itm);
}

template<class ArrItem> BoundingBox envelope_bounding_box(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::envelope_bounding_box(itm);
}

template<class ArrItem> BoundingBox fixed_bounding_box(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::fixed_bounding_box(itm);
}

template<class ArrItem> decltype(auto) envelope_convex_hull(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::envelope_convex_hull(itm);
}

template<class ArrItem> decltype(auto) fixed_convex_hull(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::fixed_convex_hull(itm);
}

template<class ArrItem> decltype(auto) envelope_outline(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::envelope_outline(itm);
}

template<class ArrItem> decltype(auto) fixed_outline(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::fixed_outline(itm);
}

template<class ArrItem> double envelope_area(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::envelope_area(itm);
}

template<class ArrItem> double fixed_area(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::fixed_area(itm);
}

template<class ArrItem> Vec2crd fixed_centroid(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::fixed_centroid(itm);
}

template<class ArrItem> Vec2crd envelope_centroid(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::envelope_centroid(itm);
}

template<class ArrItem>
auto allowed_rotations(const ArrItem &itm)
{
    return NFPArrangeItemTraits<ArrItem>::allowed_rotations(itm);
}

template<class It>
BoundingBox bounding_box(const Range<It> &itms) noexcept
{
    auto pilebb =
        std::accumulate(itms.begin(), itms.end(), BoundingBox{},
                        [](BoundingBox bb, const auto &itm) {
                            bb.merge(fixed_bounding_box(itm));
                            return bb;
                        });

    return pilebb;
}

template<class It>
BoundingBox bounding_box_on_bedidx(const Range<It> &itms, int bed_index) noexcept
{
    auto pilebb =
        std::accumulate(itms.begin(), itms.end(), BoundingBox{},
                        [bed_index](BoundingBox bb, const auto &itm) {
                            if (bed_index == get_bed_index(itm))
                                bb.merge(fixed_bounding_box(itm));

                            return bb;
                        });

    return pilebb;
}

}} // namespace Slic3r::arr2

#endif // ARRANGEITEMTRAITSNFP_HPP
