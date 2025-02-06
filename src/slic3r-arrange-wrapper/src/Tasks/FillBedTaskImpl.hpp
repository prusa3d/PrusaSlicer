///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef FILLBEDTASKIMPL_HPP
#define FILLBEDTASKIMPL_HPP

#include <boost/log/trivial.hpp>

#include <arrange/NFP/NFPArrangeItemTraits.hpp>

#include <arrange-wrapper/Tasks/FillBedTask.hpp>

namespace Slic3r { namespace arr2 {

template<class ArrItem>
int calculate_items_needed_to_fill_bed(const ExtendedBed &bed,
                                       const ArrItem &prototype_item,
                                       size_t prototype_count,
                                       const std::vector<ArrItem> &fixed)
{
    double poly_area  = fixed_area(prototype_item);

    auto area_sum_fn = [&](double s, const auto &itm) {
        return s + (get_bed_index(itm) == get_bed_constraint(prototype_item)) * fixed_area(itm);
    };

    double unsel_area = std::accumulate(fixed.begin(),
                                        fixed.end(),
                                        0.,
                                        area_sum_fn);

    double fixed_area = unsel_area + prototype_count * poly_area;
    double bed_area   = 0.;

    visit_bed([&bed_area] (auto &realbed) { bed_area = area(realbed); }, bed);

    // This is the maximum number of items,
    // the real number will always be close but less.
    auto needed_items = static_cast<int>(
        std::ceil((bed_area - fixed_area) / poly_area));

    return needed_items;
}

template<class ArrItem>
void extract(FillBedTask<ArrItem> &task,
             const Scene &scene,
             const ArrangeableToItemConverter<ArrItem> &itm_conv)
{
    task.prototype_item = {};

    auto selected_ids = scene.selected_ids();

    if (selected_ids.empty())
        return;

    std::set<ObjectID> selected_objects = selected_geometry_ids(scene);

    if (selected_objects.size() != 1)
        return;

    ObjectID prototype_geometry_id = *(selected_objects.begin());

    auto set_prototype_item = [&task, &itm_conv](const Arrangeable &arrbl) {
        if (arrbl.is_printable())
            task.prototype_item = itm_conv.convert(arrbl);
    };

    scene.model().visit_arrangeable(selected_ids.front(), set_prototype_item);

    if (!task.prototype_item)
        return;

    // Workaround for missing items when arranging the same geometry only:
    // Injecting a number of items but with slightly shrinked shape, so that
    // they can fill the emerging holes.
    ArrItem prototype_item_shrinked;
    scene.model().visit_arrangeable(selected_ids.front(),
        [&prototype_item_shrinked, &itm_conv](const Arrangeable &arrbl) {
            if (arrbl.is_printable())
                prototype_item_shrinked = itm_conv.convert(arrbl, -SCALED_EPSILON);
        });

    const int bed_constraint{*get_bed_constraint(*task.prototype_item)};
    if (bed_constraint != get_bed_index(*task.prototype_item)) {
        return;
    }

    set_bed_index(*task.prototype_item, Unarranged);

    auto collect_task_items = [&prototype_geometry_id, &task,
                               &itm_conv, &bed_constraint](const Arrangeable &arrbl) {
        try {
            if (arrbl.bed_constraint() == bed_constraint) {
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
            }
        } catch (const EmptyItemOutlineError &ex) {
            BOOST_LOG_TRIVIAL(error)
                << "ObjectID " << std::to_string(arrbl.id().id) << ": " << ex.what();
        }
    };

    scene.model().for_each_arrangeable(collect_task_items);

    int needed_items = calculate_items_needed_to_fill_bed(task.bed,
                                                          *task.prototype_item,
                                                          task.selected.size(),
                                                          task.unselected);

    task.selected_existing_count = task.selected.size();
    task.selected.reserve(task.selected.size() + needed_items);
    std::fill_n(std::back_inserter(task.selected), needed_items,
                *task.prototype_item);

    // Add as many filler items as there are needed items. Most of them will
    // be discarded anyways.
    std::fill_n(std::back_inserter(task.selected_fillers), needed_items,
                prototype_item_shrinked);
}


template<class ArrItem>
std::unique_ptr<FillBedTask<ArrItem>> FillBedTask<ArrItem>::create(
    const Scene &sc, const ArrangeableToItemConverter<ArrItem> &converter)
{
    auto task = std::make_unique<FillBedTask<ArrItem>>();

    task->settings.set_from(sc.settings());

    task->bed = get_corrected_bed(sc.bed(), converter);

    extract(*task, sc, converter);

    return task;
}

template<class ArrItem>
std::unique_ptr<FillBedTaskResult> FillBedTask<ArrItem>::process_native(
    Ctl &ctl)
{
    auto result = std::make_unique<FillBedTaskResult>();

    if (!prototype_item)
        return result;

    result->prototype_id = retrieve_id(*prototype_item).value_or(ObjectID{});

    class FillBedCtl: public ArrangerCtl<ArrItem>
    {
        ArrangeTaskCtl &parent;
        FillBedTask &self;
        bool do_stop = false;

    public:
        FillBedCtl(ArrangeTaskCtl &p, FillBedTask &slf) : parent{p}, self{slf} {}

        void update_status(int remaining) override
        {
            parent.update_status(remaining);
        }

        bool was_canceled() const override
        {
            return parent.was_canceled() || do_stop;
        }

        void on_packed(ArrItem &itm) override
        {
            // Stop at the first filler that is not on the physical bed
            do_stop = get_bed_index(itm) == -1 && get_priority(itm) == 0;
        }

    } subctl(ctl, *this);

    auto arranger = Arranger<ArrItem>::create(settings);

    arranger->arrange(selected, unselected, bed, subctl);

    auto unsel_cpy = unselected;
    for (const auto &itm : selected) {
        unsel_cpy.emplace_back(itm);
    }

    arranger->arrange(selected_fillers, unsel_cpy, bed, FillBedCtl{ctl, *this});

    auto arranged_range = Range{selected.begin(),
                                selected.begin() + selected_existing_count};

    result->add_arranged_items(arranged_range);

    auto to_add_range = Range{selected.begin() + selected_existing_count,
                              selected.end()};

    for (auto &itm : to_add_range) {
        if (get_bed_index(itm) == get_bed_constraint(itm))
            result->add_new_item(itm);
    }

    for (auto &itm : selected_fillers)
        if (get_bed_index(itm) == get_bed_constraint(itm))
            result->add_new_item(itm);

    return result;
}

} // namespace arr2
} // namespace Slic3r

#endif // FILLBEDTASKIMPL_HPP
