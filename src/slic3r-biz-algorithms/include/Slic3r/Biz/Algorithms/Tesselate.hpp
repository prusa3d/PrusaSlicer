#pragma once

#include <admesh/stl.h>
#include <vector>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"

namespace Slic3r::Biz::Algorithms::Tesselate {

const bool constexpr NORMALS_UP = false;
const bool constexpr NORMALS_DOWN = true;

std::vector<Domain::Vec3d> triangulate_expolygon_3d(
    const Domain::ExPolygon& poly, double z = 0, bool flip = NORMALS_UP
);
std::vector<Domain::Vec3d> triangulate_expolygons_3d(
    const Domain::ExPolygons& polys, double z = 0, bool flip = NORMALS_UP
);
std::vector<Domain::Vec2d> triangulate_expolygon_2d(
    const Domain::ExPolygon& poly, bool flip = NORMALS_UP
);
std::vector<Domain::Vec2d> triangulate_expolygons_2d(
    const Domain::ExPolygons& polys, bool flip = NORMALS_UP
);
std::vector<Domain::Vec2f> triangulate_expolygon_2f(
    const Domain::ExPolygon& poly, bool flip = NORMALS_UP
);
std::vector<Domain::Vec2f> triangulate_expolygons_2f(
    const Domain::ExPolygons& polys, bool flip = NORMALS_UP
);

indexed_triangle_set wall_strip(const Domain::Polygon& poly, double lower_z_mm, double upper_z_mm);

} // namespace Slic3r
