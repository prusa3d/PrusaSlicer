#pragma once

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Algorithms::DouglasPeucker {

Domain::Points douglas_peucker(const Domain::Points& src, double tolerance);

void douglas_peucker(Domain::Polygon& polygon, double tolerance);

void douglas_peucker(Domain::ExPolygon& expolygon, double tolerance);

} // namespace Slic3r::Biz::Algorithms::DouglasPeucker
