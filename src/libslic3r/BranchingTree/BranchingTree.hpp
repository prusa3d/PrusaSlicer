///|/ Copyright (c) Prusa Research 2022 - 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SUPPORTTREEBRANCHING_HPP
#define SUPPORTTREEBRANCHING_HPP

// For indexed_triangle_set
#include <admesh/stl.h>
#include <optional>
#include <utility>
#include <vector>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

struct indexed_triangle_set;

namespace Slic3r { namespace branchingtree {

// Branching tree input parameters. This is an in-line fillable structure with
// setters returning self references.
class Properties
{
    double m_max_slope       = PI / 4.;
    double m_ground_level    = 0.;
    double m_sampling_radius = .5;
    double m_max_branch_len  = 10.;

    ExPolygons m_bed_shape;

public:

    // Maximum slope for bridges of the tree
    Properties &max_slope(double val) noexcept
    {
        m_max_slope = val;
        return *this;
    }
    // Z level of the ground
    Properties &ground_level(double val) noexcept
    {
        m_ground_level = val;
        return *this;
    }
    // How far should sample points be in the mesh and the ground
    Properties &sampling_radius(double val) noexcept
    {
        m_sampling_radius = val;
        return *this;
    }
    // Shape of the print bed (ground)
    Properties &bed_shape(ExPolygons bed) noexcept
    {
        m_bed_shape = std::move(bed);
        return *this;
    }

    Properties &max_branch_length(double val) noexcept
    {
        m_max_branch_len = val;
        return *this;
    }

    double max_slope() const noexcept { return m_max_slope; }
    double ground_level() const noexcept { return m_ground_level; }
    double sampling_radius() const noexcept { return m_sampling_radius; }
    double max_branch_length() const noexcept { return m_max_branch_len; }
    const ExPolygons &bed_shape() const noexcept { return m_bed_shape; }
};

// A junction of the branching tree with position and radius.
struct Node
{
    static constexpr int ID_NONE = -1;

    int id = ID_NONE, left = ID_NONE, right = ID_NONE;

    Vec3f pos;
    float Rmin = 0.f;

    // Tracking the weight of each junction, which is essentially the sum of
    // the lenghts of all branches emanating from this junction.
    float weight = 0.f;

    Node(const Vec3f &p, float r_min = .0f) : pos{p}, Rmin{r_min}, weight{0.f}
    {}
};

inline bool is_occupied(const Node &n)
{
    return n.left != Node::ID_NONE && n.right != Node::ID_NONE;
}

// An output interface for the branching tree generator function. Consider each
// method as a callback and implement the actions that need to be done.
class Builder
{
public:
    virtual ~Builder() = default;

    // A simple bridge from junction to junction.
    virtual bool add_bridge(const Node &from, const Node &to) = 0;

    // An Y shaped structure with two starting points and a merge point below
    // them. The angles will respect the max_slope setting.
    virtual bool add_merger(const Node &node,
                            const Node &closest,
                            const Node &merge_node) = 0;

    // Add an anchor bridge to the ground (print bed)
    virtual bool add_ground_bridge(const Node &from,
                                   const Node &to) = 0;

    // Add an anchor bridge to the model body
    virtual bool add_mesh_bridge(const Node &from, const Node &to) = 0;

    virtual std::optional<Vec3f> suggest_avoidance(const Node &from,
                                                   float max_bridge_len) const
    {
        return {};
    }

    // Report nodes that can not be routed to an endpoint (model or ground)
    virtual void report_unroutable(const Node &j) = 0;

    // If returns false, the tree building process shall stop
    virtual bool is_valid() const { return true; }
};

// Build the actual tree.
// its:           The input mesh
// support_leafs: The input support points
// builder:       The output interface, describes how to build the tree
// properties:    Parameters of the tree
//
// Notes:
// The original algorithm implicitly ensures that the generated tree avoids
// the model body. This implementation uses point sampling of the mesh thus an
// explicit check is needed if the part of the tree being inserted properly
// avoids the model. This can be done in the builder implementation. Each
// method can return a boolean indicating whether the given branch can or
// cannot be inserted. If a particular path is unavailable, the algorithm
// will try a few other paths as well. If all of them fail, one of the
// report_unroutable_* methods will be called as a last resort.
void build_tree(const indexed_triangle_set &its,
                const std::vector<Node>    &support_leafs,
                Builder                    &builder,
                const Properties           &properties = {});

inline void build_tree(const indexed_triangle_set &its,
                       const std::vector<Node>    &support_leafs,
                       Builder                   &&builder,
                       const Properties           &properties = {})
{
    build_tree(its, support_leafs, builder, properties);
}

// Helper function to derive a bed polygon only from the model bounding box.
ExPolygon make_bed_poly(const indexed_triangle_set &its);

}} // namespace Slic3r::branchingtree

#endif // SUPPORTTREEBRANCHING_HPP
