///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGETASK_HPP
#define ARRANGETASK_HPP

#include <arrange-wrapper/Arrange.hpp>
#include <arrange-wrapper/Items/TrafoOnlyArrangeItem.hpp>

namespace Slic3r { namespace arr2 {

struct ArrangeTaskResult : public ArrangeResult
{
    std::vector<TrafoOnlyArrangeItem> items;

    bool apply_on(ArrangeableModel &mdl) override
    {
        bool ret = true;
        for (auto &itm : items) {
            if (is_arranged(itm))
                ret = ret && apply_arrangeitem(itm, mdl);
        }

        return ret;
    }

    template<class ArrItem>
    void add_item(const ArrItem &itm)
    {
        items.emplace_back(itm);
        if (auto id = retrieve_id(itm))
            imbue_id(items.back(), *id);
    }

    template<class It>
    void add_items(const Range<It> &items_range)
    {
        for (auto &itm : items_range)
            add_item(itm);
    }
};

template<class ArrItem> struct ArrangeTask : public ArrangeTaskBase
{
    struct ArrangeSet
    {
        std::vector<ArrItem> selected, unselected;
    } printable, unprintable;

    ExtendedBed     bed;
    ArrangeSettings settings;

    static std::unique_ptr<ArrangeTask> create(
        const Scene                        &sc,
        const ArrangeableToItemConverter<ArrItem> &converter);

    static std::unique_ptr<ArrangeTask> create(const Scene &sc)
    {
        auto conv = ArrangeableToItemConverter<ArrItem>::create(sc);
        return create(sc, *conv);
    }

    std::unique_ptr<ArrangeResult> process(Ctl &ctl) override
    {
        return process_native(ctl);
    }

    std::unique_ptr<ArrangeTaskResult> process_native(Ctl &ctl);
    std::unique_ptr<ArrangeTaskResult> process_native(Ctl &&ctl)
    {
        return process_native(ctl);
    }

    int item_count_to_process() const override
    {
        return static_cast<int>(printable.selected.size() +
                                unprintable.selected.size());
    }
};

} // namespace arr2
} // namespace Slic3r

#endif // ARRANGETASK_HPP
