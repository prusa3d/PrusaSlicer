#pragma once

#include <admesh/stl.h>
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include <numbers>

namespace Slic3r::Biz::Emboss {

struct BendParams
{
    float horizontal_bend{0.0f}; // in radians, [-pi, pi], controls in/out bend along X length (Y handle slider)
    float vertical_curl{0.0f};   // in radians, [-pi, pi], controls up/down curl along Y height (X handle slider)
};

class TextBender
{
public:
    static void bend_mesh(
        indexed_triangle_set& its,
        const BendParams& params,
        const Domain::BoundingBox3f& base_bbox
    );

    static Domain::Vec3d bend_point(
        const Domain::Vec3d& pt,
        const BendParams& params,
        const Domain::BoundingBox3f& base_bbox
    );
};

} // namespace Slic3r::Biz::Emboss
