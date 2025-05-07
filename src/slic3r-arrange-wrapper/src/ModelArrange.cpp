///|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include <libslic3r/Model.hpp>
#include <utility>

#include <arrange-wrapper/ModelArrange.hpp>

#include <arrange-wrapper/Items/ArrangeItem.hpp>
#include <arrange-wrapper/Tasks/MultiplySelectionTask.hpp>
#include <arrange-wrapper/SceneBuilder.hpp>
#include <arrange-wrapper/ArrangeSettingsView.hpp>
#include <arrange-wrapper/Scene.hpp>

namespace Slic3r {

void duplicate_objects(Model &model, size_t copies_num)
{
    for (ModelObject *o : model.objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        for (const ModelInstance *i : instances)
            for (size_t k = 2; k <= copies_num; ++ k)
                o->add_instance(*i);
    }
}

bool arrange_objects(Model &model,
                     const arr2::ArrangeBed &bed,
                     const arr2::ArrangeSettingsView &settings)
{
    return arrange(arr2::SceneBuilder{}
                       .set_bed(bed)
                       .set_arrange_settings(settings)
                       .set_model(model));
}

void duplicate_objects(Model &model,
                       size_t copies_num,
                       const arr2::ArrangeBed &bed,
                       const arr2::ArrangeSettingsView &settings)
{
    duplicate_objects(model, copies_num);
    arrange_objects(model, bed, settings);
}

void duplicate(Model &model,
               size_t copies_num,
               const arr2::ArrangeBed &bed,
               const arr2::ArrangeSettingsView &settings)
{
    auto vbh = arr2::VirtualBedHandler::create(bed);
    arr2::DuplicableModel dup_model{&model, std::move(vbh), bounding_box(bed)};

    arr2::Scene scene{arr2::BasicSceneBuilder{}
                          .set_arrangeable_model(&dup_model)
                          .set_arrange_settings(&settings)
                          .set_bed(bed)};

    if (copies_num >= 1)
        copies_num -= 1;

    auto task = arr2::MultiplySelectionTask<arr2::ArrangeItem>::create(scene, copies_num);
    auto result = task->process_native(arr2::DummyCtl{});
    if (result->apply_on(scene.model()))
        dup_model.apply_duplicates();
}

} // namespace Slic3r
