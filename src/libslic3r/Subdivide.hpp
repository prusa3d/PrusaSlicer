///|/ Copyright (c) Prusa Research 2022 Pavel Mikuš @Godrak
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_Subdivide_hpp_
#define libslic3r_Subdivide_hpp_

#include "TriangleMesh.hpp"
#include "admesh/stl.h"

namespace Slic3r {

indexed_triangle_set its_subdivide(const indexed_triangle_set &its, float max_length);

}

#endif //libslic3r_Subdivide_hpp_
