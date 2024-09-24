///|/ Copyright (c) Prusa Research 2020 - 2023 Vojtěch Bubník @bubnikv, Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SLA_SUPPORTTREE_HPP
#define SLA_SUPPORTTREE_HPP

#include <libslic3r/Polygon.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/AABBMesh.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/SupportPoint.hpp>
#include <libslic3r/SLA/JobController.hpp>
#include <libslic3r/SLA/SupportTreeStrategies.hpp>
#include <math.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

#include "admesh/stl.h"

namespace Slic3r {

namespace sla {
struct JobController;

struct SupportTreeConfig
{
    bool   enabled = true;

    // Type of the support tree, for
    SupportTreeType tree_type = SupportTreeType::Default;

    // Radius in mm of the pointing side of the head.
    double head_front_radius_mm = 0.2;

    // How much the pinhead has to penetrate the model surface
    double head_penetration_mm = 0.5;

    // Radius of the back side of the 3d arrow.
    double head_back_radius_mm = 0.5;

    double head_fallback_radius_mm = 0.25;

    // Width in mm from the back sphere center to the front sphere center.
    double head_width_mm = 1.0;

    // How to connect pillars
    PillarConnectionMode pillar_connection_mode = PillarConnectionMode::dynamic;

    // Only generate pillars that can be routed to ground
    bool ground_facing_only = false;

    // TODO: unimplemented at the moment. This coefficient will have an impact
    // when bridges and pillars are merged. The resulting pillar should be a bit
    // thicker than the ones merging into it. How much thicker? I don't know
    // but it will be derived from this value.
    double pillar_widening_factor = .5;

    // Radius in mm of the pillar base.
    double base_radius_mm = 2.0;

    // The height of the pillar base cone in mm.
    double base_height_mm = 1.0;

    // The default angle for connecting support sticks and junctions.
    double bridge_slope = M_PI/4;

    // The max length of a bridge in mm
    double max_bridge_length_mm = 10.0;

    // The max distance of a pillar to pillar link.
    double max_pillar_link_distance_mm = 10.0;

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom.
    double object_elevation_mm = 10;
    
    // The shortest distance between a pillar base perimeter from the model
    // body. This is only useful when elevation is set to zero.
    double pillar_base_safety_distance_mm = 0.5;
    
    unsigned max_bridges_on_pillar = 3;

    double max_weight_on_model_support = 10.f;

    double head_fullwidth() const {
        return 2 * head_front_radius_mm + head_width_mm +
               2 * head_back_radius_mm - head_penetration_mm;
    }

    double safety_distance() const { return safety_distance_mm; }
    double safety_distance(double r) const
    {
        return std::min(safety_distance_mm, r * safety_distance_mm / head_back_radius_mm);
    }

    // /////////////////////////////////////////////////////////////////////////
    // Compile time configuration values (candidates for runtime)
    // /////////////////////////////////////////////////////////////////////////

    // The max Z angle for a normal at which it will get completely ignored.
    static const double constexpr normal_cutoff_angle = 150.0 * M_PI / 180.0;

    // The safety gap between a support structure and model body. For support
    // struts smaller than head_back_radius, the safety distance is scaled
    // down accordingly. see method safety_distance()
    static const double constexpr safety_distance_mm = 0.5;

    static const double constexpr max_solo_pillar_height_mm = 15.0;
    static const double constexpr max_dual_pillar_height_mm = 35.0;
    static const double constexpr optimizer_rel_score_diff = 1e-10;
    static const unsigned constexpr optimizer_max_iterations = 2000;
    static const unsigned constexpr pillar_cascade_neighbors = 3;
    
};

enum class MeshType { Support, Pad };

struct SupportableMesh
{
    AABBMesh          emesh;
    SupportPoints     pts;
    SupportTreeConfig cfg;
    PadConfig         pad_cfg;
    double            zoffset = 0.;

    explicit SupportableMesh(const indexed_triangle_set &trmsh,
                             const SupportPoints        &sp,
                             const SupportTreeConfig    &c)
        : emesh{trmsh}, pts{sp}, cfg{c}
    {}

//    explicit SupportableMesh(const AABBMesh          &em,
//                             const SupportPoints     &sp,
//                             const SupportTreeConfig &c)
//        : emesh{em}, pts{sp}, cfg{c}
//    {}
};

inline double ground_level(const SupportableMesh &sm)
{
    double lvl = sm.zoffset -
                 !bool(sm.pad_cfg.embed_object) * sm.cfg.enabled * sm.cfg.object_elevation_mm +
                  bool(sm.pad_cfg.embed_object) * sm.pad_cfg.wall_thickness_mm;

    return lvl;
}

indexed_triangle_set create_support_tree(const SupportableMesh &mesh,
                                         const JobController   &ctl);

indexed_triangle_set create_pad(const SupportableMesh      &model_mesh,
                                const indexed_triangle_set &support_mesh,
                                const JobController        &ctl);

std::vector<ExPolygons> slice(const indexed_triangle_set &support_mesh,
                              const indexed_triangle_set &pad_mesh,
                              const std::vector<float>   &grid,
                              float                       closing_radius,
                              const JobController        &ctl);

} // namespace sla
} // namespace Slic3r

#endif // SLASUPPORTTREE_HPP
