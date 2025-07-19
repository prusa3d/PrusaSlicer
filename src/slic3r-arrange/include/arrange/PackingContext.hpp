///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef PACKINGCONTEXT_HPP
#define PACKINGCONTEXT_HPP

#include "ArrangeItemTraits.hpp"

namespace Slic3r { namespace arr2 {

template<class Ctx, class En = void>
struct PackingContextTraits_ {
    template<class ArrItem>
    static void add_fixed_item(Ctx &ctx, const ArrItem &itm)
    {
        ctx.add_fixed_item(itm);
    }

    template<class ArrItem>
    static void add_packed_item(Ctx &ctx, ArrItem &itm)
    {
        ctx.add_packed_item(itm);
    }

    // returns a range of all packed items in the context ctx
    static auto all_items_range(const Ctx &ctx)
    {
        return ctx.all_items_range();
    }

    static auto fixed_items_range(const Ctx &ctx)
    {
        return ctx.fixed_items_range();
    }

    static auto packed_items_range(const Ctx &ctx)
    {
        return ctx.packed_items_range();
    }

    static auto packed_items_range(Ctx &ctx)
    {
        return ctx.packed_items_range();
    }
};

template<class Ctx, class ArrItem>
void add_fixed_item(Ctx &ctx, const ArrItem &itm)
{
    PackingContextTraits_<StripCVRef<Ctx>>::add_fixed_item(ctx, itm);
}

template<class Ctx, class ArrItem>
void add_packed_item(Ctx &ctx, ArrItem &itm)
{
    PackingContextTraits_<StripCVRef<Ctx>>::add_packed_item(ctx, itm);
}

template<class Ctx>
auto all_items_range(const Ctx &ctx)
{
    return PackingContextTraits_<StripCVRef<Ctx>>::all_items_range(ctx);
}

template<class Ctx>
auto fixed_items_range(const Ctx &ctx)
{
    return PackingContextTraits_<StripCVRef<Ctx>>::fixed_items_range(ctx);
}

template<class Ctx>
auto packed_items_range(Ctx &&ctx)
{
    return PackingContextTraits_<StripCVRef<Ctx>>::packed_items_range(ctx);
}

template<class ArrItem>
class DefaultPackingContext {
    using ArrItemRaw = StripCVRef<ArrItem>;
    std::vector<std::reference_wrapper<const ArrItemRaw>> m_fixed;
    std::vector<std::reference_wrapper<ArrItemRaw>> m_packed;
    std::vector<std::reference_wrapper<const ArrItemRaw>> m_items;

public:
    DefaultPackingContext() = default;

    template<class It>
    explicit DefaultPackingContext(const Range<It> &fixed_items)
    {
        std::copy(fixed_items.begin(), fixed_items.end(), std::back_inserter(m_fixed));
        std::copy(fixed_items.begin(), fixed_items.end(), std::back_inserter(m_items));
    }

    auto all_items_range() const noexcept { return crange(m_items); }
    auto fixed_items_range() const noexcept { return crange(m_fixed); }
    auto packed_items_range() const noexcept { return crange(m_packed); }
    auto packed_items_range() noexcept { return range(m_packed); }

    void add_fixed_item(const ArrItem &itm)
    {
        m_fixed.emplace_back(itm);
        m_items.emplace_back(itm);
    }

    void add_packed_item(ArrItem &itm)
    {
        m_packed.emplace_back(itm);
        m_items.emplace_back(itm);
    }
};

template<class It>
auto default_context(const Range<It> &items)
{
    using ArrItem = StripCVRef<typename std::iterator_traits<It>::value_type>;
    return DefaultPackingContext<ArrItem>{items};
}

template<class Cont, class ArrItem = typename Cont::value_type>
auto default_context(const Cont &container)
{
    return DefaultPackingContext<ArrItem>{crange(container)};
}

}} // namespace Slic3r::arr2

#endif // PACKINGCONTEXT_HPP
