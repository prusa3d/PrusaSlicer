///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef MULTIPLYSELECTIONTASKIMPL_HPP
#define MULTIPLYSELECTIONTASKIMPL_HPP

#include <arrange-wrapper/Tasks/MultiplySelectionTask.hpp>

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace arr2 {

template<class ArrItem>
std::unique_ptr<MultiplySelectionTask<ArrItem>> MultiplySelectionTask<ArrItem>::create(
    const Scene &scene, size_t count, const ArrangeableToItemConverter<ArrItem> &itm_conv)
{
    auto task_ptr = std::make_unique<MultiplySelectionTask<ArrItem>>();

    auto &task = *task_ptr;

    task.settings.set_from(scene.settings());

    task.bed = get_corrected_bed(scene.bed(), itm_conv);

    task.prototype_item = {};

    auto selected_ids = scene.selected_ids();

    if (selected_ids.empty())
        return task_ptr;

    std::set<ObjectID> selected_objects = selected_geometry_ids(scene);

    if (selected_objects.size() != 1)
        return task_ptr;

    ObjectID prototype_geometry_id = *(selected_objects.begin());

    auto set_prototype_item = [&task, &itm_conv](const Arrangeable &arrbl) {
        if (arrbl.is_printable())
            task.prototype_item = itm_conv.convert(arrbl);
    };

    scene.model().visit_arrangeable(selected_ids.front(), set_prototype_item);

    if (!task.prototype_item)
        return task_ptr;

    set_bed_index(*task.prototype_item, Unarranged);

    auto collect_task_items = [&prototype_geometry_id, &task,
                               &itm_conv](const Arrangeable &arrbl) {
        try {
            if (arrbl.geometry_id() == prototype_geometry_id) {
                if (arrbl.is_printable()) {
                    auto itm = itm_conv.convert(arrbl);
                    raise_priority(itm);
                    task.selected.emplace_back(std::move(itm));
                }
            } else {
                auto itm = itm_conv.convert(arrbl, -SCALED_EPSILON);
                task.unselected.emplace_back(std::move(itm));
            }
        } catch (const EmptyItemOutlineError &ex) {
            BOOST_LOG_TRIVIAL(error)
                << "ObjectID " << std::to_string(arrbl.id().id) << ": " << ex.what();
        }
    };

    scene.model().for_each_arrangeable(collect_task_items);

    task.selected_existing_count = task.selected.size();
    task.selected.reserve(task.selected.size() + count);
    std::fill_n(std::back_inserter(task.selected), count, *task.prototype_item);

    return task_ptr;
}

template<class ArrItem>
std::unique_ptr<MultiplySelectionTaskResult>
MultiplySelectionTask<ArrItem>::process_native(Ctl &ctl)
{
    auto result = std::make_unique<MultiplySelectionTaskResult>();

    if (!prototype_item)
        return result;

    result->prototype_id = retrieve_id(*prototype_item).value_or(ObjectID{});

    class MultiplySelectionCtl: public ArrangerCtl<ArrItem>
    {
        ArrangeTaskCtl &parent;
        MultiplySelectionTask<ArrItem> &self;

    public:
        MultiplySelectionCtl(ArrangeTaskCtl &p, MultiplySelectionTask<ArrItem> &slf)
            : parent{p}, self{slf} {}

        void update_status(int remaining) override
        {
            parent.update_status(remaining);
        }

        bool was_canceled() const override
        {
            return parent.was_canceled();
        }

    } subctl(ctl, *this);

    auto arranger = Arranger<ArrItem>::create(settings);

    arranger->arrange(selected, unselected, bed, subctl);

    auto arranged_range = Range{selected.begin(),
                                selected.begin() + selected_existing_count};

    result->add_arranged_items(arranged_range);

    auto to_add_range = Range{selected.begin() + selected_existing_count,
                              selected.end()};

    result->add_new_items(to_add_range);

    return result;
}

}} // namespace Slic3r::arr2

#endif // MULTIPLYSELECTIONTASKIMPL_HPP
