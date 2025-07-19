///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGEBASE_HPP
#define ARRANGEBASE_HPP

#include <iterator>
#include <type_traits>

#include <libslic3r/Point.hpp>

#include <arrange/ArrangeItemTraits.hpp>
#include <arrange/PackingContext.hpp>

namespace Slic3r { namespace arr2 {

namespace detail_is_const_it {

template<class It, class En = void>
struct IsConstIt_ { static constexpr bool value = false; };

template<class It>
using iterator_category_t = typename std::iterator_traits<It>::iterator_category;

template<class It>
using iterator_reference_t = typename std::iterator_traits<It>::reference;

template<class It>
struct IsConstIt_ <It, std::enable_if_t<std::is_class_v<iterator_category_t<It>>> >
{
    static constexpr bool value =
        std::is_const_v<std::remove_reference_t<iterator_reference_t<It>>>;
};

} // namespace detail_is_const_it

template<class It>
static constexpr bool IsConstIterator = detail_is_const_it::IsConstIt_<It>::value;

template<class It>
constexpr bool is_const_iterator(const It &it) noexcept { return IsConstIterator<It>; }

// The pack() function will use tag dispatching, based on the given strategy
// object that is used as its first argument.

// This tag is derived for a packing strategy as default, and will be used
// to cast a compile error.
struct UnimplementedPacking {};

// PackStrategyTag_ needs to be specialized for any valid packing strategy class
template<class PackStrategy> struct PackStrategyTag_ {
    using Tag = UnimplementedPacking;
};

// Helper metafunc to derive packing strategy tag from a strategy object.
template<class Strategy>
using PackStrategyTag =
    typename PackStrategyTag_<remove_cvref_t<Strategy>>::Tag;


template<class PackStrategy, class En = void> struct PackStrategyTraits_ {
    template<class ArrItem> using Context = DefaultPackingContext<ArrItem>;

    template<class ArrItem, class Bed>
    static Context<ArrItem> create_context(PackStrategy &ps,
                                           const Bed &bed,
                                           int bed_index)
    {
        return {};
    }
};

template<class PS> using PackStrategyTraits = PackStrategyTraits_<StripCVRef<PS>>;

template<class PS, class ArrItem>
using PackStrategyContext =
    typename PackStrategyTraits<PS>::template Context<StripCVRef<ArrItem>>;

template<class ArrItem, class PackStrategy, class Bed>
PackStrategyContext<PackStrategy, ArrItem> create_context(PackStrategy &&ps,
                                                          const Bed &bed,
                                                          int bed_index)
{
    return PackStrategyTraits<PackStrategy>::template create_context<
        StripCVRef<ArrItem>>(ps, bed, bed_index);
}

// Function to pack one item into a bed.
// strategy parameter holds clue to what packing strategy to use. This function
//          needs to be overloaded for the strategy tag belonging to the given
//          strategy.
// 'bed' parameter is the type of bed into which the new item should be packed.
//       See beds.hpp for valid bed classes.
// 'item' parameter is the item to be packed. After succesful arrangement
//        (see return value) the item will have it's translation and rotation
//        set correctly. If the function returns false, the translation and
//        rotation of the input item might be changed to arbitrary values.
// 'fixed_items' paramter holds a range of ArrItem type objects that are already
//               on the bed and need to be avoided by the newly packed item.
// 'remaining_items' is a range of ArrItem type objects that are intended to be
//                   packed in the future. This information can be leveradged by
//                   the packing strategy to make more intelligent placement
//                   decisions for the input item.
template<class Strategy, class Bed, class ArrItem, class RemIt>
bool pack(Strategy &&strategy,
          const Bed &bed,
          ArrItem &item,
          const PackStrategyContext<Strategy, ArrItem> &context,
          const Range<RemIt> &remaining_items)
{
    static_assert(IsConstIterator<RemIt>, "Remaining item iterator is not const!");

    // Dispatch:
    return pack(std::forward<Strategy>(strategy), bed, item, context,
                remaining_items, PackStrategyTag<Strategy>{});
}

// Overload without fixed items:
template<class Strategy, class Bed, class ArrItem>
bool pack(Strategy &&strategy, const Bed &bed, ArrItem &item)
{
    std::vector<ArrItem> dummy;
    auto context = create_context<ArrItem>(strategy, bed, PhysicalBedId);
    return pack(std::forward<Strategy>(strategy), bed, item, context,
                crange(dummy));
}

// Overload when strategy is unkown, yields compile error:
template<class Strategy, class Bed, class ArrItem, class RemIt>
bool pack(Strategy &&strategy,
          const Bed &bed,
          ArrItem &item,
          const PackStrategyContext<Strategy, ArrItem> &context,
          const Range<RemIt> &remaining_items,
          const UnimplementedPacking &)
{
    static_assert(always_false<Strategy>::value,
                  "Packing unimplemented for this placement strategy");

    return false;
}

// Helper function to remove unpackable items from the input container.
template<class PackStrategy, class Container, class Bed, class StopCond>
void remove_unpackable_items(PackStrategy  &&ps,
                             Container      &c,
                             const Bed      &bed,
                             const StopCond &stopcond)
{
    // Safety test: try to pack each item into an empty bed. If it fails
    // then it should be removed from the list
    auto it = c.begin();
    while (it != c.end() && !stopcond()) {
        StripCVRef<decltype(*it)> &itm = *it;
        auto cpy{itm};

        if (!pack(ps, bed, cpy)) {
            set_bed_index(itm, Unarranged);
            it = c.erase(it);
        } else
            it++;
    }
}

// arrange() function will use tag dispatching based on the selection strategy
// given as its first argument.

// This tag is derived for a selection strategy as default, and will be used
// to cast a compile error.
struct UnimplementedSelection {};

// SelStrategyTag_ needs to be specialized for any valid selection strategy class
template<class SelStrategy> struct SelStrategyTag_ {
    using Tag = UnimplementedSelection;
};

// Helper metafunc to derive the selection strategy tag from a strategy object.
template<class Strategy>
using SelStrategyTag = typename SelStrategyTag_<remove_cvref_t<Strategy>>::Tag;

// Main function to start the arrangement. Takes a selection and a packing
// strategy object as the first two parameters. An implementation
// (function overload) must exist for this function that takes the coresponding
// selection strategy tag belonging to the given selstrategy argument.
//
// items parameter is a range of arrange items to arrange.
// fixed parameter is a range of arrange items that have fixed position and will
//                 not move during the arrangement but need to be avoided by the
//                 moving items.
// bed parameter is the type of bed into which the items need to fit.
template<class It,
         class ConstIt,
         class TBed,
         class SelectionStrategy,
         class PackStrategy>
void arrange(SelectionStrategy &&selstrategy,
             PackStrategy &&packingstrategy,
             const Range<It> &items,
             const Range<ConstIt> &fixed,
             const TBed &bed)
{
    static_assert(IsConstIterator<ConstIt>, "Fixed item iterator is not const!");

    // Dispatch:
    arrange(std::forward<SelectionStrategy>(selstrategy),
            std::forward<PackStrategy>(packingstrategy), items, fixed, bed,
            SelStrategyTag<SelectionStrategy>{});
}

template<class It, class TBed, class SelectionStrategy, class PackStrategy>
void arrange(SelectionStrategy &&selstrategy,
             PackStrategy &&packingstrategy,
             const Range<It> &items,
             const TBed &bed)
{
    std::vector<typename std::iterator_traits<It>::value_type> dummy;
    arrange(std::forward<SelectionStrategy>(selstrategy),
            std::forward<PackStrategy>(packingstrategy), items, crange(dummy),
            bed);
}

// Overload for unimplemented selection strategy, yields compile error:
template<class It,
         class ConstIt,
         class TBed,
         class SelectionStrategy,
         class PackStrategy>
void arrange(SelectionStrategy &&selstrategy,
             PackStrategy &&packingstrategy,
             const Range<It> &items,
             const Range<ConstIt> &fixed,
             const TBed &bed,
             const UnimplementedSelection &)
{
    static_assert(always_false<SelectionStrategy>::value,
                  "Arrange unimplemented for this selection strategy");
}

template<class It>
std::vector<int> get_bed_indices(const Range<It> &items)
{
    auto bed_indices = reserve_vector<int>(items.size());

    for (auto &itm : items)
        bed_indices.emplace_back(get_bed_index(itm));

    std::sort(bed_indices.begin(), bed_indices.end());
    auto endit = std::unique(bed_indices.begin(), bed_indices.end());

    bed_indices.erase(endit, bed_indices.end());

    return bed_indices;
}

template<class It, class CIt>
std::vector<int> get_bed_indices(const Range<It> &items, const Range<CIt> &fixed)
{
    std::vector<int> ret;

    auto iitems = get_bed_indices(items);
    auto ifixed = get_bed_indices(fixed);
    ret.reserve(std::max(iitems.size(), ifixed.size()));
    std::set_union(iitems.begin(), iitems.end(),
                   ifixed.begin(), ifixed.end(),
                   std::back_inserter(ret));

    return ret;
}

template<class It>
size_t get_bed_count(const Range<It> &items)
{
    return get_bed_indices(items).size();
}

template<class It> int get_max_bed_index(const Range<It> &items)
{
    auto it = std::max_element(items.begin(),
                               items.end(),
                               [](auto &i1, auto &i2) {
                                   return get_bed_index(i1) < get_bed_index(i2);
                               });

    int ret = Unarranged;
    if (it != items.end())
        ret = get_bed_index(*it);

    return ret;
}

struct DefaultStopCondition {
    constexpr bool operator()() const noexcept { return false; }
};

}} // namespace Slic3r::arr2

#endif // ARRANGEBASE_HPP
