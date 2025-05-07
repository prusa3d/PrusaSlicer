///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef MULTIPLYSELECTIONTASK_HPP
#define MULTIPLYSELECTIONTASK_HPP

#include <arrange-wrapper/Arrange.hpp>
#include <arrange-wrapper/Items/TrafoOnlyArrangeItem.hpp>

namespace Slic3r { namespace arr2 {

struct MultiplySelectionTaskResult: public ArrangeResult {
    ObjectID prototype_id;

    std::vector<TrafoOnlyArrangeItem> arranged_items;
    std::vector<TrafoOnlyArrangeItem> to_add;

    bool apply_on(ArrangeableModel &mdl) override
    {
        bool ret = prototype_id.valid();

        if (!ret)
            return ret;

        for (auto &itm : to_add) {
            auto id = mdl.add_arrangeable(prototype_id);
            imbue_id(itm, id);
            ret = ret && apply_arrangeitem(itm, mdl);
        }

        for (auto &itm : arranged_items) {
            if (is_arranged(itm))
                ret = ret && apply_arrangeitem(itm, mdl);
        }

        return ret;
    }

    template<class ArrItem>
    void add_arranged_item(const ArrItem &itm)
    {
        arranged_items.emplace_back(itm);
        if (auto id = retrieve_id(itm))
            imbue_id(arranged_items.back(), *id);
    }

    template<class It>
    void add_arranged_items(const Range<It> &items_range)
    {
        arranged_items.reserve(items_range.size());
        for (auto &itm : items_range)
            add_arranged_item(itm);
    }

    template<class ArrItem> void add_new_item(const ArrItem &itm)
    {
        to_add.emplace_back(itm);
    }

    template<class It> void add_new_items(const Range<It> &items_range)
    {
        to_add.reserve(items_range.size());
        for (auto &itm : items_range) {
            to_add.emplace_back(itm);
        }
    }
};

template<class ArrItem>
struct MultiplySelectionTask: public ArrangeTaskBase
{
    std::optional<ArrItem> prototype_item;

    std::vector<ArrItem> selected, unselected;

    ArrangeSettings settings;
    ExtendedBed bed;
    size_t selected_existing_count = 0;

    std::unique_ptr<MultiplySelectionTaskResult> process_native(Ctl &ctl);
    std::unique_ptr<MultiplySelectionTaskResult> process_native(Ctl &&ctl)
    {
        return process_native(ctl);
    }

    std::unique_ptr<ArrangeResult> process(Ctl &ctl) override
    {
        return process_native(ctl);
    }

    int item_count_to_process() const override
    {
        return selected.size();
    }

    static std::unique_ptr<MultiplySelectionTask> create(
        const Scene &sc,
        size_t multiply_count,
        const ArrangeableToItemConverter<ArrItem> &converter);

    static std::unique_ptr<MultiplySelectionTask> create(const Scene &sc,
                                                         size_t multiply_count)
    {
        auto conv = ArrangeableToItemConverter<ArrItem>::create(sc);
        return create(sc, multiply_count, *conv);
    }
};

}} // namespace Slic3r::arr2

#endif // MULTIPLYSELECTIONTASK_HPP
