///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef NFPCONCAVE_CGAL_HPP
#define NFPCONCAVE_CGAL_HPP

#include <libslic3r/ExPolygon.hpp>

#include "libslic3r/Polygon.hpp"

namespace Slic3r {

Polygons convex_decomposition_cgal(const Polygon &expoly);
Polygons convex_decomposition_cgal(const ExPolygon &expoly);
ExPolygons nfp_concave_concave_cgal(const ExPolygon &fixed, const ExPolygon &movable);

} // namespace Slic3r

#endif // NFPCONCAVE_CGAL_HPP
