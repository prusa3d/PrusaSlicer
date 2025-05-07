///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGEFIRSTFIT_HPP
#define ARRANGEFIRSTFIT_HPP

#include <iterator>
#include <map>

#include <arrange/ArrangeBase.hpp>

namespace Slic3r { namespace arr2 { namespace firstfit {

struct SelectionTag {};

// Can be specialized by Items
template<class ArrItem, class En = void>
struct ItemArrangedVisitor {
    template<class Bed, class PIt, class RIt>
    static void on_arranged(ArrItem &itm,
                            const Bed &bed,
                            const Range<PIt> &packed_items,
                            const Range<RIt> &remaining_items)
    {}
};

// Use the the visitor baked into the ArrItem type by default
struct DefaultOnArrangedFn {
    template<class ArrItem, class Bed, class PIt, class RIt>
    void operator()(ArrItem &itm,
                    const Bed &bed,
                    const Range<PIt> &packed,
                    const Range<RIt> &remaining)
    {
        ItemArrangedVisitor<StripCVRef<ArrItem>>::on_arranged(itm, bed, packed,
                                                              remaining);
    }
};

struct DefaultItemCompareFn {
    template<class ArrItem>
    bool operator() (const ArrItem &ia, const ArrItem &ib)
    {
        return get_priority(ia) > get_priority(ib);
    }
};

template<class CompareFn  = DefaultItemCompareFn,
         class OnArrangedFn = DefaultOnArrangedFn,
         class StopCondition = DefaultStopCondition>
struct SelectionStrategy
{
    CompareFn  cmpfn;
    OnArrangedFn on_arranged_fn;
    StopCondition cancel_fn;

    SelectionStrategy(CompareFn cmp = {},
             OnArrangedFn on_arranged = {},
             StopCondition stopcond = {})
        : cmpfn{cmp},
          on_arranged_fn{std::move(on_arranged)},
          cancel_fn{std::move(stopcond)}
    {}
};

} // namespace firstfit

template<class... Args> struct SelStrategyTag_<firstfit::SelectionStrategy<Args...>> {
    using Tag = firstfit::SelectionTag;
};

template<class It,
         class ConstIt,
         class TBed,
         class SelStrategy,
         class PackStrategy>
void arrange(
    SelStrategy &&sel,
    PackStrategy &&ps,
    const Range<It> &items,
    const Range<ConstIt> &fixed,
    const TBed &bed,
    const firstfit::SelectionTag &)
{
    using ArrItem = typename std::iterator_traits<It>::value_type;
    using ArrItemRef = std::reference_wrapper<ArrItem>;

    auto sorted_items = reserve_vector<ArrItemRef>(items.size());

    for (auto &itm : items) {
        set_bed_index(itm, Unarranged);
        sorted_items.emplace_back(itm);
    }

    using Context = PackStrategyContext<PackStrategy, ArrItem>;

    std::map<int, Context> bed_contexts;
    auto get_or_init_context = [&ps, &bed, &bed_contexts](int bedidx) -> Context& {
        auto ctx_it = bed_contexts.find(bedidx);
        if (ctx_it == bed_contexts.end()) {
            auto res = bed_contexts.emplace(
                bedidx, create_context<ArrItem>(ps, bed, bedidx));

            assert(res.second);

            ctx_it = res.first;
        }

        return ctx_it->second;
    };

    for (auto &itm : fixed) {
        auto bedidx = get_bed_index(itm);
        if (bedidx >= 0) {
            Context &ctx = get_or_init_context(bedidx);
            add_fixed_item(ctx, itm);
        }
    }

    if constexpr (!std::is_null_pointer_v<decltype(sel.cmpfn)>) {
        std::stable_sort(sorted_items.begin(), sorted_items.end(), sel.cmpfn);
    }

    auto is_cancelled = [&sel]() {
        return sel.cancel_fn();
    };

    remove_unpackable_items(ps, sorted_items, bed, [&is_cancelled]() {
        return is_cancelled();
    });

    auto it = sorted_items.begin();

    using SConstIt = typename std::vector<ArrItemRef>::const_iterator;

    while (it != sorted_items.end() && !is_cancelled()) {
        bool was_packed = false;
        int bedidx = 0;
        while (!was_packed && !is_cancelled()) {
            for (; !was_packed && !is_cancelled(); bedidx++) {
                const std::optional<int> bed_constraint{get_bed_constraint(*it)};
                if (bed_constraint && bedidx != *bed_constraint) {
                    continue;
                }
                set_bed_index(*it, bedidx);

                auto remaining = Range{std::next(static_cast<SConstIt>(it)),
                                       sorted_items.cend()};

                Context &ctx = get_or_init_context(bedidx);

                was_packed = pack(ps, bed, *it, ctx, remaining);

                if(was_packed) {
                    add_packed_item(ctx, *it);

                    auto packed_range = Range{sorted_items.cbegin(),
                                              static_cast<SConstIt>(it)};

                    sel.on_arranged_fn(*it, bed, packed_range, remaining);
                } else {
                    set_bed_index(*it, Unarranged);
                    if (bed_constraint && bedidx == *bed_constraint) {
                        // Leave the item as is as it does not fit on the enforced bed.
                        auto packed_range = Range{sorted_items.cbegin(),
                                                  static_cast<SConstIt>(it)};
                        was_packed = true;
                        sel.on_arranged_fn(*it, bed, packed_range, remaining);
                    }
                }
            }
        }
        ++it;
    }
}

}} // namespace Slic3r::arr2

#endif // ARRANGEFIRSTFIT_HPP
