///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef KERNELTRAITS_HPP
#define KERNELTRAITS_HPP

#include <arrange/ArrangeItemTraits.hpp>

namespace Slic3r { namespace arr2 {

// An arrangement kernel that specifies the object function to the arrangement
// optimizer and additional callback functions to be able to track the state
// of the arranged pile during arrangement.
template<class Kernel, class En = void> struct KernelTraits_
{
    // Has to return a score value marking the quality of the arrangement. The
    // higher this value is, the better a particular placement of the item is.
    // parameter transl is the translation needed for the item to be moved to
    // the candidate position.
    // To discard the item, return NaN as score for every translation.
    template<class ArrItem>
    static double placement_fitness(const Kernel  &k,
                                    const ArrItem &itm,
                                    const Vec2crd &transl)
    {
        return k.placement_fitness(itm, transl);
    }

    // Called whenever a new item is about to be processed by the optimizer.
    // The current state of the arrangement can be saved by the kernel: the
    // already placed items and the remaining items that need to fit into a
    // particular bed.
    // Returns true if the item is can be packed immediately, false if it
    // should be processed further. This way, a kernel have the power to
    // choose an initial position for the item that is not on the NFP.
    template<class ArrItem, class Bed, class Ctx, class RemIt>
    static bool on_start_packing(Kernel &k,
                                 ArrItem &itm,
                                 const Bed &bed,
                                 const Ctx &packing_context,
                                 const Range<RemIt> &remaining_items)
    {
        return k.on_start_packing(itm, bed, packing_context, remaining_items);
    }

    // Called when an item has been succesfully packed. itm should have the
    // final translation and rotation already set.
    // Can return false to discard the item after the optimization.
    template<class ArrItem>
    static bool on_item_packed(Kernel &k, ArrItem &itm)
    {
        return k.on_item_packed(itm);
    }
};

template<class K> using KernelTraits = KernelTraits_<StripCVRef<K>>;

}} // namespace Slic3r::arr2

#endif // KERNELTRAITS_HPP
