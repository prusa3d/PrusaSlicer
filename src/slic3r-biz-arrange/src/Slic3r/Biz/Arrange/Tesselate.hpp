#pragma once

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Arrange {

Domain::Polygons convex_decomposition_tess(const Domain::Polygon& expoly);
Domain::Polygons convex_decomposition_tess(const Domain::ExPolygon& expoly);
Domain::Polygons convex_decomposition_tess(const Domain::ExPolygons& expolys);
Domain::ExPolygons nfp_concave_concave_tess(
    const Domain::ExPolygon& fixed,
    const Domain::ExPolygon& movable
);

} // namespace Slic3r::Biz::Arrange
