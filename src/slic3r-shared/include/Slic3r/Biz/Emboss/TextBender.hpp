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
    float vertical_arc{0.0f};   // in radians, [-pi, pi], arcs text width up/down in its XY face (Z handle slider)
};

class TextBender
{
public:
    // Maximum edge chord error in millimeters after subdivision.
    static constexpr double MAX_CHORD_ERROR = 0.05;
    static double clamp_angle(double radians);

    // Pre-tessellate a drag baseline once for the entire supported angle range.
    static void prepare_mesh(indexed_triangle_set& its, const Domain::BoundingBox3d& base_bbox);

    // Prefer the saved original bounds when available, especially for composed
    // bends at +/-180 degrees where bent bounds cannot recover them accurately.
    static void restore_mesh(indexed_triangle_set& its, const BendParams& params,
        const Domain::BoundingBox3d& base_bbox);

    static void bend_mesh(
        indexed_triangle_set& its,
        const BendParams& params,
        const Domain::BoundingBox3f& base_bbox
    );

    static void bend_mesh(
        indexed_triangle_set& its,
        const BendParams& params,
        const Domain::BoundingBox3d& base_bbox
    );

    static Domain::Vec3d bend_point(
        const Domain::Vec3d& pt,
        const BendParams& params,
        const Domain::BoundingBox3f& base_bbox
    );

    // Mesh inversion accepts the CURRENT bent bounds and recovers the original
    // arc dimensions. Point inversion below accepts the ORIGINAL base bounds.
    static void unbend_mesh(
        indexed_triangle_set& its,
        const BendParams& params,
        const Domain::BoundingBox3f& base_bbox
    );

    static void unbend_mesh(
        indexed_triangle_set& its,
        const BendParams& params,
        const Domain::BoundingBox3d& base_bbox
    );

    static Domain::Vec3d unbend_point(
        const Domain::Vec3d& pt,
        const BendParams& params,
        const Domain::BoundingBox3f& base_bbox
    );
};

} // namespace Slic3r::Biz::Emboss
