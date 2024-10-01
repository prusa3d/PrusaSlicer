///|/ Copyright (c) Prusa Research 2020 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef RASTERTOPOLYGONS_HPP
#define RASTERTOPOLYGONS_HPP

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"

namespace Slic3r {
namespace sla {

class RasterGrayscaleAA;

ExPolygons raster_to_polygons(const RasterGrayscaleAA &rst, Vec2i windowsize = {2, 2});

}} // namespace Slic3r::sla

#endif // RASTERTOPOLYGONS_HPP
