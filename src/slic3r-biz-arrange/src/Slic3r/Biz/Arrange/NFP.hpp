#pragma once

#include <boost/variant.hpp>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Arrange {

// Convex-Convex nfp in linear time (fixed.size() + movable.size()),
// no memory allocations (if out param is used).
// FIXME: Currently broken for very sharp triangles.
Domain::Polygon nfp_convex_convex(const Domain::Polygon& fixed, const Domain::Polygon& movable);
void nfp_convex_convex(const Domain::Polygon& fixed, const Domain::Polygon& movable, Domain::Polygon& out);
Domain::Polygon nfp_convex_convex_legacy(const Domain::Polygon& fixed, const Domain::Polygon& movable);
Domain::Polygon ifp_convex_convex(const Domain::Polygon& fixed, const Domain::Polygon& movable);

Domain::Vec2crd reference_vertex(const Domain::Polygon& outline);
Domain::Vec2crd reference_vertex(const Domain::ExPolygon& outline);
Domain::Vec2crd reference_vertex(const Domain::Polygons& outline);
Domain::Vec2crd reference_vertex(const Domain::ExPolygons& outline);

Domain::Vec2crd min_vertex(const Domain::Polygon& outline);

} // namespace Slic3r::Biz::Arrange
