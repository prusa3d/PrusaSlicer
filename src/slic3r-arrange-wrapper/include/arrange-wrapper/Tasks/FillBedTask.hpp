///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef FILLBEDTASK_HPP
#define FILLBEDTASK_HPP

#include <arrange-wrapper/Arrange.hpp>

#include "MultiplySelectionTask.hpp"

namespace Slic3r { namespace arr2 {

struct FillBedTaskResult: public MultiplySelectionTaskResult {};

template<class ArrItem>
struct FillBedTask: public ArrangeTaskBase
{
    std::optional<ArrItem> prototype_item;

    std::vector<ArrItem> selected, unselected;

    // For workaround regarding "holes" when filling the bed with the same
    // item's copies
    std::vector<ArrItem> selected_fillers;

    ArrangeSettings settings;
    ExtendedBed bed;
    size_t selected_existing_count = 0;

    std::unique_ptr<FillBedTaskResult> process_native(Ctl &ctl);
    std::unique_ptr<FillBedTaskResult> process_native(Ctl &&ctl)
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

    static std::unique_ptr<FillBedTask> create(
        const Scene &sc,
        const ArrangeableToItemConverter<ArrItem> &converter);

    static std::unique_ptr<FillBedTask> create(const Scene &sc)
    {
        auto conv = ArrangeableToItemConverter<ArrItem>::create(sc);
        return create(sc, *conv);
    }
};

} // namespace arr2
} // namespace Slic3r

#endif // FILLBEDTASK_HPP
