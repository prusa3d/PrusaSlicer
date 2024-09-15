///|/ Copyright (c) Prusa Research 2018 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include <libslic3r/Arrange/Scene.hpp>
#include <stddef.h>
#include <vector>
#include <cstddef>

#include "libslic3r/Arrange/Core/Beds.hpp"

namespace Slic3r {

class Model;
class ModelInstance;

namespace arr2 {
class ArrangeSettingsView;
}  // namespace arr2

using ModelInstancePtrs = std::vector<ModelInstance*>;

//void duplicate(Model &model, ArrangePolygons &copies, VirtualBedFn);
void duplicate_objects(Model &model, size_t copies_num);

bool arrange_objects(Model &model,
                     const arr2::ArrangeBed &bed,
                     const arr2::ArrangeSettingsView &settings);

void duplicate_objects(Model &              model,
                       size_t               copies_num,
                       const arr2::ArrangeBed &bed,
                       const arr2::ArrangeSettingsView &settings);

void duplicate(Model &              model,
               size_t               copies_num,
               const arr2::ArrangeBed &bed,
               const arr2::ArrangeSettingsView &settings);

} // namespace Slic3r

#endif // MODELARRANGE_HPP
