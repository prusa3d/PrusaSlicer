#pragma once

#include <admesh/stl.h>

#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"

namespace Slic3r::Domain {

struct indexed_triangle_set_with_color
{
    std::vector<stl_triangle_vertex_indices> indices;
    std::vector<stl_vertex>                  vertices;
    std::vector<uint8_t>                     colors;
};

enum class AdditionalMeshInfo {
    None,
    Color
};

template<AdditionalMeshInfo mesh_info> struct IndexedTriangleSetType;

template<> struct IndexedTriangleSetType<AdditionalMeshInfo::None>
{
    using type = indexed_triangle_set;
};

template<> struct IndexedTriangleSetType<AdditionalMeshInfo::Color>
{
    using type = indexed_triangle_set_with_color;
};

struct RepairedMeshErrors {
    // How many edges were united by merging their end points with some other end points in epsilon neighborhood?
    int           edges_fixed               = 0;
    // How many degenerate faces were removed?
    int           degenerate_facets         = 0;
    // How many faces were removed during fixing? Includes degenerate_faces and disconnected faces.
    int           facets_removed            = 0;
    // New faces could only be created with stl_fill_holes() and we ditched stl_fill_holes(), because mostly it does more harm than good.
    //int          facets_added             = 0;
    // How many facets were revesed? Faces are reversed by admesh while it connects patches of triangles togeter and a flipped triangle is encountered.
    // Also the facets are reversed when a negative volume is corrected by flipping all facets.
    int           facets_reversed           = 0;
    // Edges shared by two triangles, oriented incorrectly.
    int           backwards_edges           = 0;

    void clear() { *this = RepairedMeshErrors(); }

    void merge(const RepairedMeshErrors& rhs) {
        this->edges_fixed         += rhs.edges_fixed;
        this->degenerate_facets   += rhs.degenerate_facets;
        this->facets_removed      += rhs.facets_removed;
        this->facets_reversed     += rhs.facets_reversed;
        this->backwards_edges     += rhs.backwards_edges;
    }

    bool repaired() const { return degenerate_facets > 0 || edges_fixed > 0 || facets_removed > 0 || facets_reversed > 0 || backwards_edges > 0; }
};

struct TriangleMeshStats {
    // Mesh metrics.
    stl_vertex    max                       = stl_vertex::Zero();
    stl_vertex    min                       = stl_vertex::Zero();
    stl_vertex    size                      = stl_vertex::Zero();
    float         volume                    = -1.f;
    int           number_of_parts           = 0;

    // Mesh errors, remaining.
    int           open_edges                = 0;

    // Mesh errors, fixed.
    RepairedMeshErrors repaired_errors;

    void clear() { *this = TriangleMeshStats(); }

    TriangleMeshStats merge(const TriangleMeshStats& rhs) const
    {
        TriangleMeshStats out;
        out.min             = this->min.cwiseMin(rhs.min);
        out.max             = this->max.cwiseMax(rhs.max);
        out.size            = out.max - out.min;
        out.number_of_parts = this->number_of_parts + rhs.number_of_parts;
        out.open_edges      = this->open_edges + rhs.open_edges;
        out.volume          = this->volume + rhs.volume;
        out.repaired_errors.merge(rhs.repaired_errors);
        return out;
    }

    bool manifold() const { return open_edges == 0; }
    bool repaired() const { return repaired_errors.repaired(); }
};

class TriangleMesh
{
public:
    TriangleMesh() = default;
    TriangleMesh(indexed_triangle_set&& its);
    TriangleMesh(indexed_triangle_set&& its, TriangleMeshStats&& stats);
    TriangleMesh(const indexed_triangle_set& its, const TriangleMeshStats& stats);

    float volume();
    void scale(float factor);
    void scale(const Domain::Vec3f &versor);
    void translate(const Domain::Vec3f &displacement);
    void rotate(float angle, const Domain::Axis &axis);
    void rotate(double angle, Domain::Point* center);
    void mirror(const Domain::Axis axis);
    void transform(const Domain::Transform3d& t, bool fix_left_handed = false);
    // Flip triangles, negate volume.
    void flip_triangles();
    void align_to_origin();
    void merge(const TriangleMesh &mesh);
    Domain::BoundingBox3d bounding_box() const;
    // Return the size of the mesh in coordinates.
    Domain::Vec3d size() const { return m_stats.size.cast<double>(); }
    // Returns the convex hull of this TriangleMesh
    size_t facets_count() const { return this->its.indices.size(); }
    bool   empty() const { return this->facets_count() == 0; }
    bool   has_zero_volume() const;
    // Estimate of the memory occupied by this structure, important for keeping an eye on the Undo / Redo stack allocation.
    size_t memsize() const;

    // Used by the Undo / Redo stack, legacy interface. As of now there is nothing cached at TriangleMesh,
    // but we may decide to cache some data in the future (for example normals), thus we keep the interface in place.
    // Release optional data from the mesh if the object is on the Undo / Redo stack only. Returns the amount of memory released.
    size_t release_optional() { return 0; }
    // Restore optional data possibly released by release_optional().
    void   restore_optional() {}

    const TriangleMeshStats& stats() const { return m_stats; }

    indexed_triangle_set its;

private:
    TriangleMeshStats m_stats;
};

float its_volume(const indexed_triangle_set &its);

// After applying a transformation with negative determinant, flip the faces to keep the transformed mesh volume positive.
void its_flip_triangles(indexed_triangle_set &its);

inline Domain::BoundingBox3d bounding_box(const TriangleMesh &m) { return m.bounding_box(); }
inline Domain::BoundingBox3d bounding_box(const indexed_triangle_set& its)
{
    if (its.vertices.empty())
        return {};

    Domain::Vec3f bmin = its.vertices.front(), bmax = its.vertices.front();

    for (const Domain::Vec3f &p : its.vertices) {
        bmin = p.cwiseMin(bmin);
        bmax = p.cwiseMax(bmax);
    }

    return {bmin.cast<double>(), bmax.cast<double>()};
}

inline Domain::BoundingBox3d bounding_box(const indexed_triangle_set& its, const Domain::Transform3f &tr)
{
    if (its.vertices.empty())
        return {};

    Domain::Vec3f bmin = tr * its.vertices.front(), bmax = tr * its.vertices.front();

    for (const Domain::Vec3f &p : its.vertices) {
        Domain::Vec3f pp = tr * p;
        bmin = pp.cwiseMin(bmin);
        bmax = pp.cwiseMax(bmax);
    }

    return {bmin.cast<double>(), bmax.cast<double>()};
}

/// <summary>
/// Merge one triangle mesh to another
/// Added triangle set will be consumed
/// </summary>
/// <param name="its">IN/OUT triangle mesh</param>
/// <param name="its_add">Triangle mesh (will be consumed)</param>
void its_merge(indexed_triangle_set &its, indexed_triangle_set &&its_add);

void its_merge(indexed_triangle_set &A, const indexed_triangle_set &B);
void its_merge(indexed_triangle_set &A, const std::vector<Domain::Vec3f> &triangles);
void its_merge(indexed_triangle_set &A, const Domain::Vec3ds &triangles);

using its_triangle = std::array<stl_vertex, 3>;

inline its_triangle its_triangle_vertices(const indexed_triangle_set &its,
                                          const Domain::Index3 &face)
{
    return {its.vertices[face[0]],
            its.vertices[face[1]],
            its.vertices[face[2]]};
}

inline its_triangle its_triangle_vertices(const indexed_triangle_set &its,
                                          size_t                      face_id)
{
    return its_triangle_vertices(its, its.indices[face_id]);
}
}
