#ifndef slic3r_TriangleMesh_hpp_
#define slic3r_TriangleMesh_hpp_

#include <admesh/stl.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <vector>
#include <Eigen/Geometry>
#include <array>
#include <utility>
#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <numbers>
#include <optional>

#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

namespace Slic3r::Biz::Algorithms::TriangleMesh {

Domain::TriangleMeshStats calculate_stats(const indexed_triangle_set& its);

Domain::TriangleMesh construct(
    const std::vector<Domain::Vec3f>& vertices,
    const std::vector<Domain::Index3>& faces
);
Domain::TriangleMesh construct(std::vector<Domain::Vec3f>&& vertices, std::vector<Domain::Index3>&& faces);
Domain::TriangleMesh construct(const indexed_triangle_set& its);
Domain::TriangleMesh construct(
    indexed_triangle_set&& its,
    const Domain::RepairedMeshErrors& errors = Domain::RepairedMeshErrors{}
);
Domain::TriangleMesh construct(std::vector<stl_facet>&& facets, bool repair = true);

Domain::Vec3d center(const Domain::TriangleMesh& mesh);

bool is_splittable(const Domain::TriangleMesh& mesh);
Domain::Polygon convex_hull(const Domain::TriangleMesh& mesh);
Domain::TriangleMesh convex_hull_3d(const Domain::TriangleMesh& mesh);

/**
@brief Compare TriangleMeshes by Bounding boxes (mainly for sort)
From Front(Z) Upper(Y) TopLeft(X) corner.
1. Seraparate group not overlaped i Z axis
2. Seraparate group not overlaped i Y axis
3. Start earlier in X (More on left side)
@param triangle_mesh1 Compare from
@param triangle_mesh2 Compare to
@return True when triangle mesh 1 is closer, upper or lefter than triangle mesh 2 other wise false
*/
bool is_front_up_left(
    const Domain::TriangleMesh& trinagle_mesh1,
    const Domain::TriangleMesh& triangle_mesh2
);
std::vector<Domain::TriangleMesh> split(const Domain::TriangleMesh& mesh);
// Returns the bbox of the given TriangleMesh transformed by the given transformation
Domain::BoundingBox3d transformed_bounding_box(
    const Domain::TriangleMesh& mesh,
    const Domain::Transform3d& trafo
);

// Returns the bbox of the part of the given TriangleMesh, transformed by the given transformation,
// above the given z in world coordinates.
Domain::BoundingBox3d transformed_bounding_box(const Domain::TriangleMesh& mesh, const Domain::Transform3d& trafo, double world_z);

/**
 * Returns false when the output file could not be opened for writing.
 */
bool write_obj_file(const Domain::TriangleMesh& mesh, const char* output_file);

void trianglemesh_repair_on_import(stl_file& stl);

// Index of face indices incident with a vertex index.
struct VertexFaceIndex
{
public:
    using iterator = std::vector<size_t>::const_iterator;

    VertexFaceIndex(const indexed_triangle_set& its)
    {
        this->create(its);
    }

    VertexFaceIndex() {}

    void create(const indexed_triangle_set& its);

    void clear()
    {
        m_vertex_to_face_start.clear();
        m_vertex_faces_all.clear();
    }

    // Iterators of face indices incident with the input vertex_id.
    iterator begin(size_t vertex_id) const throw()
    {
        return m_vertex_faces_all.begin() + m_vertex_to_face_start[vertex_id];
    }

    iterator end(size_t vertex_id) const throw()
    {
        return m_vertex_faces_all.begin() + m_vertex_to_face_start[vertex_id + 1];
    }

    // Vertex incidence.
    size_t count(size_t vertex_id) const throw()
    {
        return m_vertex_to_face_start[vertex_id + 1] - m_vertex_to_face_start[vertex_id];
    }

    const Range<iterator> operator[](size_t vertex_id) const
    {
        return {begin(vertex_id), end(vertex_id)};
    }

    std::size_t memsize() const {
        std::size_t result{};
        result += m_vertex_to_face_start.capacity() * sizeof(std::size_t);
        result += m_vertex_faces_all.capacity() * sizeof(std::size_t);
        return result;
    }

private:
    std::vector<size_t> m_vertex_to_face_start;
    std::vector<size_t> m_vertex_faces_all;
};

// Map from a face edge to a unique edge identifier or -1 if no neighbor exists.
// Two neighbor faces share a unique edge identifier even if they are flipped.
// Used for chaining slice lines into polygons.
template <Domain::AdditionalMeshInfo mesh_info = Domain::AdditionalMeshInfo::None>
std::vector<Domain::Index3> its_face_edge_ids(
    const typename Domain::IndexedTriangleSetType<mesh_info>::type& its
);

std::vector<Domain::Index3> its_face_edge_ids(
    const indexed_triangle_set& its,
    std::function<void()> throw_on_cancel_callback
);

template <Domain::AdditionalMeshInfo mesh_info = Domain::AdditionalMeshInfo::None>
std::vector<Domain::Index3> its_face_edge_ids(
    const typename Domain::IndexedTriangleSetType<mesh_info>::type& its,
    const std::vector<char>& face_mask
);

// Having the face neighbors available, assign unique edge IDs to face edges for chaining of polygons over slices.
std::vector<Domain::Index3> its_face_edge_ids(
    const indexed_triangle_set& its,
    std::vector<Domain::Index3>& face_neighbors,
    bool assign_unbound_edges = false,
    int* num_edges            = nullptr
);

// Create index that gives neighbor faces for each face. Ignores face orientations.
std::vector<Domain::Index3> its_face_neighbors(const indexed_triangle_set& its);
std::vector<Domain::Index3> its_face_neighbors_par(const indexed_triangle_set& its);

// Merge duplicate vertices, return number of vertices removed.
// This function will happily create non-manifolds if more than two faces share the same vertex position
// or more than two faces share the same edge position!
int its_merge_vertices(indexed_triangle_set& its, bool shrink_to_fit = true);

// Calculate number of degenerate faces. There should be no degenerate faces in a nice mesh.
int its_num_degenerate_faces(const indexed_triangle_set& its);
// Remove degenerate faces, return number of faces removed.
int its_remove_degenerate_faces(indexed_triangle_set& its, bool shrink_to_fit = true);

// Remove vertices, which none of the faces references. Return number of freed vertices.
int its_compactify_vertices(indexed_triangle_set& its, bool shrink_to_fit = true);

// store part of index triangle set
bool its_store_triangle_to_obj(
    const indexed_triangle_set& its,
    const char* obj_filename,
    size_t triangle_index
);
bool its_store_triangles_to_obj(
    const indexed_triangle_set& its,
    const char* obj_filename,
    const std::vector<size_t>& triangles
);

std::vector<indexed_triangle_set> its_split(const indexed_triangle_set& its);
std::vector<indexed_triangle_set> its_split(
    const indexed_triangle_set& its,
    std::vector<Domain::Index3>& face_neighbors
);

// Number of disconnected patches (faces are connected if they share an edge, shared edge defined with 2 shared vertex indices).
size_t its_number_of_patches(const indexed_triangle_set& its);
size_t its_number_of_patches(
    const indexed_triangle_set& its,
    const std::vector<Domain::Index3>& face_neighbors
);
// Same as its_number_of_patches(its) > 1, but faster.
bool its_is_splittable(const indexed_triangle_set& its);
bool its_is_splittable(const indexed_triangle_set& its, const std::vector<Domain::Index3>& face_neighbors);

// Calculate number of unconnected face edges. There should be no unconnected edge in a manifold mesh.
size_t its_num_open_edges(const indexed_triangle_set& its);
size_t its_num_open_edges(const std::vector<Domain::Index3>& face_neighbors);

// Calculate and returns the list of unconnected face edges.
// Each edge is represented by the indices of the two endpoint vertices
std::vector<std::pair<int, int>> its_get_open_edges(const indexed_triangle_set& its);

// Shrink the vectors of its.vertices and its.faces to a minimum size by reallocating the two vectors.
void its_shrink_to_fit(indexed_triangle_set& its);

// For convex hull calculation: Transform mesh, trim it by the Z plane and collect all vertices. Duplicate vertices will be produced.
void its_collect_mesh_projection_points_above(
    const indexed_triangle_set& its,
    const Domain::SquareMatrix3f& m,
    const float z,
    Domain::Points& all_pts
);
void its_collect_mesh_projection_points_above(
    const indexed_triangle_set& its,
    const Domain::Transform3f& t,
    const float z,
    Domain::Points& all_pts
);

// Calculate 2D convex hull of a transformed and clipped mesh. Uses the function above.
Domain::Polygon its_convex_hull_2d_above(
    const indexed_triangle_set& its,
    const Domain::SquareMatrix3f& m,
    const float z
);
Domain::Polygon its_convex_hull_2d_above(
    const indexed_triangle_set& its,
    const Domain::Transform3f& t,
    const float z
);

// Index of a vertex inside triangle_indices.
inline int its_triangle_vertex_index(const stl_triangle_vertex_indices& triangle_indices, int vertex_idx)
{
    return vertex_idx == triangle_indices[0] ? 0 :
        vertex_idx == triangle_indices[1]    ? 1 :
        vertex_idx == triangle_indices[2]    ? 2 :
                                               -1;
}

inline Domain::Index2 its_triangle_edge(const stl_triangle_vertex_indices& triangle_indices, int edge_idx)
{
    int next_edge_idx = (edge_idx == 2) ? 0 : edge_idx + 1;
    return {triangle_indices[edge_idx], triangle_indices[next_edge_idx]};
}

// Index of an edge inside triangle.
inline int its_triangle_edge_index(
    const stl_triangle_vertex_indices& triangle_indices,
    const std::array<int, 2>& triangle_edge
)
{
    return triangle_edge[0] == triangle_indices[0] && triangle_edge[1] == triangle_indices[1] ? 0 :
        triangle_edge[0] == triangle_indices[1] && triangle_edge[1] == triangle_indices[2]    ? 1 :
        triangle_edge[0] == triangle_indices[2] && triangle_edge[1] == triangle_indices[0]    ? 2 :
                                                                                                -1;
}

inline stl_normal its_unnormalized_normal(const indexed_triangle_set& its, size_t face_id)
{
    Domain::its_triangle tri = Domain::its_triangle_vertices(its, face_id);
    return (tri[1] - tri[0]).cross(tri[2] - tri[0]);
}

float its_average_edge_length(const indexed_triangle_set& its);

std::vector<Domain::Vec3f> its_face_normals(const indexed_triangle_set& its);

inline Domain::Vec3f face_normal(const stl_vertex vertex[3])
{
    return (vertex[1] - vertex[0]).cross(vertex[2] - vertex[1]).normalized();
}

inline Domain::Vec3f face_normal_normalized(const stl_vertex vertex[3])
{
    return face_normal(vertex).normalized();
}

inline Domain::Vec3f its_face_normal(const indexed_triangle_set& its, const stl_triangle_vertex_indices face)
{
    const stl_vertex vertices[3]{its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]]};
    return face_normal_normalized(vertices);
}

inline Domain::Vec3f its_face_normal(const indexed_triangle_set& its, const int face_idx)
{
    return its_face_normal(its, its.indices[face_idx]);
}

indexed_triangle_set its_make_tetrahedron(float size = 1.f);
indexed_triangle_set its_make_cube(double x, double y, double z);
indexed_triangle_set its_make_prism(float width, float length, float height);
indexed_triangle_set its_make_cylinder(double r, double h, double fa = (2 * std::numbers::pi / 360));
indexed_triangle_set its_make_cone(double r, double h, double fa = (2 * std::numbers::pi / 360));
indexed_triangle_set its_make_frustum(double r, double h, double fa = (2 * std::numbers::pi / 360));
indexed_triangle_set its_make_frustum_dowel(double r, double h, int sectorCount);
indexed_triangle_set its_make_pyramid(float base, float height);
indexed_triangle_set its_make_sphere(double radius, double fa);
indexed_triangle_set its_make_snap(
    double r,
    double h,
    float space_proportion = 0.25f,
    float bulge_proportion = 0.125f
);
indexed_triangle_set its_make_torus(
    double r,
    double t,
    double ra = (2 * std::numbers::pi / 360),
    double ta = (2 * std::numbers::pi / 360)
);

indexed_triangle_set its_convex_hull(const std::vector<Domain::Vec3f>& pts);

inline indexed_triangle_set its_convex_hull(const indexed_triangle_set& its)
{
    return its_convex_hull(its.vertices);
}

inline Domain::TriangleMesh make_cube(double x, double y, double z)
{
    using Biz::Algorithms::TriangleMesh::construct;
    return construct(its_make_cube(x, y, z));
}

inline Domain::TriangleMesh make_prism(float width, float length, float height)
{
    using Biz::Algorithms::TriangleMesh::construct;
    return construct(its_make_prism(width, length, height));
}

inline Domain::TriangleMesh make_cylinder(double r, double h, double fa = (2 * std::numbers::pi / 360))
{
    using Biz::Algorithms::TriangleMesh::construct;
    return construct(its_make_cylinder(r, h, fa));
}

inline Domain::TriangleMesh make_cone(double r, double h, double fa = (2 * std::numbers::pi / 360))
{
    using Biz::Algorithms::TriangleMesh::construct;
    return construct(its_make_cone(r, h, fa));
}

inline Domain::TriangleMesh make_pyramid(float base, float height)
{
    using Biz::Algorithms::TriangleMesh::construct;
    return construct(its_make_pyramid(base, height));
}

inline Domain::TriangleMesh make_sphere(double rho, double fa = (2 * std::numbers::pi / 360))
{
    using Biz::Algorithms::TriangleMesh::construct;
    return construct(its_make_sphere(rho, fa));
}

inline Domain::TriangleMesh make_torus(
    double r,
    double t,
    double ra = (2 * std::numbers::pi / 360),
    double ta = (2 * std::numbers::pi / 360)
)
{
    using Biz::Algorithms::TriangleMesh::construct;
    return construct(its_make_torus(r, t, ra, ta));
}

} // namespace Slic3r::Biz::Algorithms::TriangleMesh

#endif
