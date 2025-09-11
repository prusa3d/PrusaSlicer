///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <arrange-wrapper/Scene.hpp>
#include <arrange-wrapper/Items/ArrangeItem.hpp>
#include <arrange-wrapper/Tasks/ArrangeTask.hpp>
#include <arrange-wrapper/Tasks/FillBedTask.hpp>

namespace Slic3r { namespace arr2 {

std::vector<ObjectID> Scene::selected_ids() const
{
    auto items = reserve_vector<ObjectID>(model().arrangeable_count());

    model().for_each_arrangeable([ &items](auto &arrbl) mutable {
        if (arrbl.is_selected())
            items.emplace_back(arrbl.id());
    });

    return items;
}

using DefaultArrangeItem = ArrangeItem;

std::unique_ptr<ArrangeTaskBase> ArrangeTaskBase::create(Tasks task_type, const Scene &sc)
{
    std::unique_ptr<ArrangeTaskBase> ret;
    switch(task_type) {
    case Tasks::Arrange:
        ret = ArrangeTask<ArrangeItem>::create(sc);
        break;
    case Tasks::FillBed:
        ret = FillBedTask<ArrangeItem>::create(sc);
        break;
    default:
        ;
    }

    return ret;
}

std::set<ObjectID> selected_geometry_ids(const Scene &sc)
{
    std::set<ObjectID> result;

    std::vector<ObjectID> selected_ids = sc.selected_ids();
    for (const ObjectID &id : selected_ids) {
        sc.model().visit_arrangeable(id, [&result](const Arrangeable &arrbl) {
            auto id = arrbl.geometry_id();
            if (id.valid())
                result.insert(arrbl.geometry_id());
        });
    }

    return result;
}

bool arrange(Scene &scene, ArrangeTaskCtl &ctl)
{
    auto task = ArrangeTaskBase::create(Tasks::Arrange, scene);
    auto result = task->process(ctl);
    return result->apply_on(scene.model());
}

}} // namespace Slic3r::arr2
