///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARRANGETASK_IMPL_HPP
#define ARRANGETASK_IMPL_HPP

#include <random>

#include <boost/log/trivial.hpp>

#include <libslic3r/SVG.hpp>

#include <arrange-wrapper/Tasks/ArrangeTask.hpp>
#include <arrange-wrapper/Items/ArrangeItem.hpp>

namespace Slic3r { namespace arr2 {

// Prepare the selected and unselected items separately. If nothing is
// selected, behaves as if everything would be selected.
template<class ArrItem>
void extract_selected(ArrangeTask<ArrItem> &task,
                      const ArrangeableModel &mdl,
                      const ArrangeableToItemConverter<ArrItem> &itm_conv)
{
    // Go through the objects and check if inside the selection
    mdl.for_each_arrangeable(
        [&task, &itm_conv](const Arrangeable &arrbl) {
            bool selected = arrbl.is_selected();
            bool printable = arrbl.is_printable();

            try {
                auto itm = itm_conv.convert(arrbl, selected ? 0 : -SCALED_EPSILON);

                auto &container_parent = printable ? task.printable :
                                                 task.unprintable;

                auto &container = selected ?
                                       container_parent.selected :
                                       container_parent.unselected;

                container.emplace_back(std::move(itm));
            } catch (const EmptyItemOutlineError &ex) {
                BOOST_LOG_TRIVIAL(error)
                    << "ObjectID " << std::to_string(arrbl.id().id) << ": " << ex.what();
            }
        });
}

template<class ArrItem>
std::unique_ptr<ArrangeTask<ArrItem>> ArrangeTask<ArrItem>::create(
    const Scene &sc, const ArrangeableToItemConverter<ArrItem> &converter)
{
    auto task = std::make_unique<ArrangeTask<ArrItem>>();

    task->settings.set_from(sc.settings());

    task->bed = get_corrected_bed(sc.bed(), converter);

    extract_selected(*task, sc.model(), converter);

    return task;
}

// Remove all items on the physical bed (not occupyable for unprintable items)
// and shift all items to the next lower bed index, so that arrange will think
// that logical bed no. 1 is the physical one
template<class ItemCont>
void prepare_fixed_unselected(ItemCont &items, int shift)
{
    for (auto &itm : items)
        set_bed_index(itm, get_bed_index(itm) - shift);

    items.erase(std::remove_if(items.begin(), items.end(),
                               [](auto &itm) { return !is_arranged(itm); }),
                items.end());
}

inline int find_first_empty_bed(const std::vector<int>& bed_indices,
                                int starting_from = 0) {
    int ret = starting_from;

    for (int idx : bed_indices) {
        if (idx == ret) {
            ret++;
        } else if (idx > ret) {
            break;
        }
    }

    return ret;
}

template<class ArrItem>
std::unique_ptr<ArrangeTaskResult>
ArrangeTask<ArrItem>::process_native(Ctl &ctl)
{
    auto result = std::make_unique<ArrangeTaskResult>();

    auto arranger = Arranger<ArrItem>::create(settings);

    class TwoStepArrangeCtl: public Ctl
    {
        Ctl &parent;
        ArrangeTask &self;
    public:
        TwoStepArrangeCtl(Ctl &p, ArrangeTask &slf) : parent{p}, self{slf} {}

        void update_status(int remaining) override
        {
            parent.update_status(remaining + self.unprintable.selected.size());
        }

        bool was_canceled() const override { return parent.was_canceled(); }

    } subctl{ctl, *this};

    arranger->arrange(printable.selected, printable.unselected, bed, subctl);

    std::vector<int> printable_bed_indices =
        get_bed_indices(crange(printable.selected), crange(printable.unselected));

    // If there are no printables, leave the physical bed empty
    static constexpr int SearchFrom = 1;

    // Unprintable items should go to the first logical (!) bed not containing
    // any printable items
    int first_empty_bed = find_first_empty_bed(printable_bed_indices, SearchFrom);

    prepare_fixed_unselected(unprintable.unselected, first_empty_bed);

    arranger->arrange(unprintable.selected, unprintable.unselected, bed, ctl);

    result->add_items(crange(printable.selected));

    for (auto &itm : unprintable.selected) {
        if (is_arranged(itm)) {
            int bedidx = get_bed_index(itm) + first_empty_bed;
            arr2::set_bed_index(itm, bedidx);
        }

        result->add_item(itm);
    }

    return result;
}

} // namespace arr2
} // namespace Slic3r

#endif //ARRANGETASK_IMPL_HPP
