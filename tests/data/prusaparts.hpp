#ifndef PRUSAPARTS_H
#define PRUSAPARTS_H

#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"

using TestData = std::vector<Slic3r::Domain::Polygon>;
using TestDataEx = std::vector<Slic3r::Domain::ExPolygons>;

extern const TestData PRUSA_PART_POLYGONS;
extern const TestData PRUSA_STEGOSAUR_POLYGONS;
extern const TestDataEx PRUSA_PART_POLYGONS_EX;

#endif // PRUSAPARTS_H
