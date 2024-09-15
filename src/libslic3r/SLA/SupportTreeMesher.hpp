///|/ Copyright (c) Prusa Research 2020 - 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SUPPORTTREEMESHER_HPP
#define SUPPORTTREEMESHER_HPP

#include <stddef.h>
#include <tuple>
#include <cstddef>

#include "libslic3r/Point.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "admesh/stl.h"
#include "libslic3r/libslic3r.h"
//#include "libslic3r/SLA/Contour3D.hpp"

namespace Slic3r { namespace sla {

using Portion = std::tuple<double, double>;

inline Portion make_portion(double a, double b)
{
    return std::make_tuple(a, b);
}

indexed_triangle_set sphere(double  rho,
                            Portion portion = make_portion(0., PI),
                            double  fa      = (2. * PI / 360.));

// Down facing cylinder in Z direction with arguments:
// r: radius
// h: height
// ssteps: how many edges will create the base circle
// sp: starting point
inline indexed_triangle_set cylinder(double       r,
                              double       h,
                              size_t       steps = 45)
{
    return its_make_cylinder(r, h, 2 * PI / steps);
}

indexed_triangle_set pinhead(double r_pin,
                             double r_back,
                             double length,
                             size_t steps = 45);

indexed_triangle_set halfcone(double       baseheight,
                              double       r_bottom,
                              double       r_top,
                              const Vec3d &pt    = Vec3d::Zero(),
                              size_t       steps = 45);

indexed_triangle_set get_mesh(const Head &h, size_t steps);

inline indexed_triangle_set get_mesh(const Pillar &p, size_t steps)
{
    if(p.height > EPSILON) { // Endpoint is below the starting point
        // We just create a bridge geometry with the pillar parameters and
        // move the data.
        //return cylinder(p.r_start, p.height, steps, p.endpoint());
        return halfcone(p.height, p.r_end, p.r_start, p.endpt, steps);
    }

    return {};
}

inline indexed_triangle_set get_mesh(const Pedestal &p, size_t steps)
{
    return halfcone(p.height, p.r_bottom, p.r_top, p.pos, steps);
}

inline indexed_triangle_set get_mesh(const Junction &j, size_t steps)
{
    indexed_triangle_set mesh = sphere(j.r, make_portion(0, PI), 2 * PI / steps);
    Vec3f pos = j.pos.cast<float>();
    for(auto& p : mesh.vertices) p += pos;
    return mesh;
}

indexed_triangle_set get_mesh(const Bridge &br, size_t steps);

indexed_triangle_set get_mesh(const DiffBridge &br, size_t steps);

}} // namespace Slic3r::sla

#endif // SUPPORTTREEMESHER_HPP
