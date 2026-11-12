#pragma once
#include "Slic3r/Domain/EmbossShape.hpp" // ExPolygonsWithIds
#include "Slic3r/Domain/BoundingBox.hpp"

namespace Slic3r::Biz::Algorithms::ExPolygonsWithId {
void translate(Domain::ExPolygonsWithIds& e, const Domain::Point& p);
Domain::BoundingBox2crd get_extents(const Domain::ExPolygonsWithIds& e);
void center(Domain::ExPolygonsWithIds& e);
} // namespace Slic3r::Biz::Algorithms::ExPolygonsWithId
