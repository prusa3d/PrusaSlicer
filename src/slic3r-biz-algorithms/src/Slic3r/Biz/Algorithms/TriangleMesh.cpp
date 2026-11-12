#include <libqhullcpp/Qhull.h>
#include <libqhullcpp/QhullFacetList.h>
#include <libqhullcpp/QhullVertexSet.h>
#include <Slic3r/Log.hpp>
#include <boost/nowide/cstdio.hpp>
#include <libqhull_r/user_r.h>
#include <libqhullcpp/QhullFacet.h>
#include <libqhullcpp/QhullPoint.h>
#include <libqhullcpp/QhullVertex.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_for.h>
#include <cmath>
#include <vector>
#include <utility>
#include <algorithm>
#include <iterator>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Slic3r/Biz/Algorithms/MeshSplitImpl.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/Geometry.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp"
#include "Slic3r/Utils.hpp"
#include "admesh/stl.h"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

namespace Slic3r::Biz::Algorithms::TriangleMesh {

using Domain::coord_t;
using Domain::Axis;
using Domain::Point;
using Domain::Points;
using Domain::Vec2f;
using Domain::Vec2d;
using Domain::Vec3d;
using Domain::Vec3ds;
using Domain::Vec3f;
using Domain::Polygon;
using Domain::Polygons;
using Domain::BoundingBox3d;
using Domain::Transform3f;
using Domain::Transform3d;
using Domain::SquareMatrix3f;
using Domain::SquareMatrix3d;
using Slic3r::Biz::Algorithms::Scaling::scaled;
using Domain::Index3;
using Domain::TriangleMesh;
using Domain::TriangleMeshStats;
using Domain::RepairedMeshErrors;
using Domain::IndexedTriangleSetType;
using Domain::AdditionalMeshInfo;

TriangleMeshStats calculate_stats(const indexed_triangle_set& its)
{
    TriangleMeshStats result;
    result.volume = Domain::its_volume(its);
    const BoundingBox3d bbox = Domain::bounding_box(its);
    result.min = bbox.min.cast<float>();
    result.max = bbox.max.cast<float>();
    result.size = result.max - result.min;

    const std::vector<Index3> face_neighbors = its_face_neighbors(its);
    result.number_of_parts = its_number_of_patches(its, face_neighbors);
    result.open_edges = its_num_open_edges(face_neighbors);
    return result;
}

// #define SLIC3R_TRACE_REPAIR

void trianglemesh_repair_on_import(stl_file &stl)
{
    // admesh fails when repairing empty meshes
    if (stl.stats.number_of_facets == 0)
        return;

    SPDLOG_DEBUG("TriangleMesh::repair() started");

    // checking exact
#ifdef SLIC3R_TRACE_REPAIR
    SPDLOG_TRACE("\tstl_check_faces_exact");
#endif /* SLIC3R_TRACE_REPAIR */
    assert(stl_validate(&stl));
    stl_check_facets_exact(&stl);
    assert(stl_validate(&stl));
    stl.stats.facets_w_1_bad_edge = (stl.stats.connected_facets_2_edge - stl.stats.connected_facets_3_edge);
    stl.stats.facets_w_2_bad_edge = (stl.stats.connected_facets_1_edge - stl.stats.connected_facets_2_edge);
    stl.stats.facets_w_3_bad_edge = (stl.stats.number_of_facets - stl.stats.connected_facets_1_edge);
    
    // checking nearby
    //int last_edges_fixed = 0;
    float tolerance = (float)stl.stats.shortest_edge;
    float increment = (float)stl.stats.bounding_diameter / 10000.0f;
    int iterations = 2;
    if (stl.stats.connected_facets_3_edge < int(stl.stats.number_of_facets)) {
        // Not a manifold, some triangles have unconnected edges.
        for (int i = 0; i < iterations; ++ i) {
            if (stl.stats.connected_facets_3_edge < int(stl.stats.number_of_facets)) {
                // Still not a manifold, some triangles have unconnected edges.
                //printf("Checking nearby. Tolerance= %f Iteration=%d of %d...", tolerance, i + 1, iterations);
#ifdef SLIC3R_TRACE_REPAIR
                SPDLOG_TRACE("\tstl_check_faces_nearby");
#endif /* SLIC3R_TRACE_REPAIR */
                stl_check_facets_nearby(&stl, tolerance);
                //printf("  Fixed %d edges.\n", stl.stats.edges_fixed - last_edges_fixed);
                //last_edges_fixed = stl.stats.edges_fixed;
                tolerance += increment;
            } else {
                break;
            }
        }
    }
    assert(stl_validate(&stl));
    
    // remove_unconnected
    if (stl.stats.connected_facets_3_edge < (int)stl.stats.number_of_facets) {
#ifdef SLIC3R_TRACE_REPAIR
        SPDLOG_TRACE("\tstl_remove_unconnected_facets");
#endif /* SLIC3R_TRACE_REPAIR */
        stl_remove_unconnected_facets(&stl);
        assert(stl_validate(&stl));
    }
    
    // fill_holes
#if 0
    // Don't fill holes, the current algorithm does more harm than good on complex holes.
    // Rather let the slicing algorithm close gaps in 2D slices.
    if (stl.stats.connected_facets_3_edge < stl.stats.number_of_facets) {
#ifdef SLIC3R_TRACE_REPAIR
        SPDLOG_TRACE("\tstl_fill_holes");
#endif /* SLIC3R_TRACE_REPAIR */
        stl_fill_holes(&stl);
        stl_clear_error(&stl);
    }
#endif

    // normal_directions
#ifdef SLIC3R_TRACE_REPAIR
    SPDLOG_TRACE("\tstl_fix_normal_directions");
#endif /* SLIC3R_TRACE_REPAIR */
    stl_fix_normal_directions(&stl);
    assert(stl_validate(&stl));

    // normal_values
#ifdef SLIC3R_TRACE_REPAIR
    SPDLOG_TRACE("\tstl_fix_normal_values");
#endif /* SLIC3R_TRACE_REPAIR */
    stl_fix_normal_values(&stl);
    assert(stl_validate(&stl));
    
    // always calculate the volume and reverse all normals if volume is negative
#ifdef SLIC3R_TRACE_REPAIR
    SPDLOG_TRACE("\tstl_calculate_volume");
#endif /* SLIC3R_TRACE_REPAIR */
    // If the volume is negative, all the facets are flipped and added to stats.facets_reversed.
    stl_calculate_volume(&stl);
    assert(stl_validate(&stl));
    
    // neighbors
#ifdef SLIC3R_TRACE_REPAIR
    SPDLOG_TRACE("\tstl_verify_neighbors");
#endif /* SLIC3R_TRACE_REPAIR */
    stl_verify_neighbors(&stl);
    assert(stl_validate(&stl));

    //FIXME The admesh repair function may break the face connectivity, rather refresh it here as the slicing code relies on it.
    if (auto nr_degenerated = stl.stats.degenerate_facets; stl.stats.number_of_facets > 0 && nr_degenerated > 0)
        stl_check_facets_exact(&stl);

    SPDLOG_DEBUG("TriangleMesh::repair() finished");
}

TriangleMesh construct(const std::vector<Vec3f> &vertices, const std::vector<Index3> &faces)
{
    indexed_triangle_set its{faces, vertices};
    TriangleMeshStats stats{calculate_stats(its)};
    return {std::move(its), std::move(stats)};
}

TriangleMesh construct(std::vector<Vec3f> &&vertices, std::vector<Index3> &&faces)
{
    indexed_triangle_set its{std::move(faces), std::move(vertices)};
    TriangleMeshStats stats{calculate_stats(its)};
    return {std::move(its), std::move(stats)};
}

TriangleMesh construct(const indexed_triangle_set &its)
{
    const TriangleMeshStats stats{calculate_stats(its)};
    return {its, stats};
}

TriangleMesh construct(indexed_triangle_set &&its, const RepairedMeshErrors& errors/* = RepairedMeshErrors()*/)
{
    TriangleMeshStats stats{calculate_stats(its)};
    stats.repaired_errors = errors;
    return {std::move(its), std::move(stats)};
}

TriangleMesh construct(std::vector<stl_facet> &&facets, bool repair)
{
    stl_file stl;
    stl.stats.type                = inmemory;
    stl.stats.number_of_facets    = uint32_t(facets.size());
    stl.stats.original_num_facets = int(stl.stats.number_of_facets);

    stl_allocate(&stl);
    stl.facet_start               = std::move(facets);

    if (repair) {
        trianglemesh_repair_on_import(stl);
    }

    indexed_triangle_set its;
    stl_generate_shared_vertices(&stl, its);
    TriangleMeshStats stats{calculate_stats(its)};

    return {std::move(its), std::move(stats)};
}

Domain::Vec3d center(const TriangleMesh& mesh) {
    return Biz::Algorithms::BoundingBox::center(mesh.bounding_box());
}

/**
 * Calculates whether or not the mesh is splittable.
 */
bool is_splittable(const TriangleMesh& mesh)
{
    return its_is_splittable(mesh.its);
}

// 2D convex hull of a 3D mesh projected into the Z=0 plane.
Polygon convex_hull(const TriangleMesh& mesh)
{
    Points pp;
    pp.reserve(mesh.its.vertices.size());
    for (size_t i = 0; i < mesh.its.vertices.size(); ++ i) {
        const stl_vertex &v = mesh.its.vertices[i];
        pp.emplace_back(scaled(Vec2d(v(0), v(1))));
    }
    using Biz::Algorithms::Geometry::convex_hull;
    return convex_hull(pp);
}

BoundingBox3d transformed_bounding_box(const TriangleMesh& mesh, const Transform3d &trafo)
{
    using Biz::Algorithms::BoundingBox::merge;
    BoundingBox3d bbox;
    for (const stl_vertex &v : mesh.its.vertices)
        bbox = merge(bbox, trafo * v.cast<double>());
    return bbox;
}

BoundingBox3d transformed_bounding_box(const Domain::TriangleMesh& mesh, const Domain::Transform3d& trafo, double world_z)
{
    // 1) Allocate transformed vertices with their position with respect to print bed surface.
    std::vector<char>            sides;
    size_t                       num_above = 0;
    Eigen::AlignedBox<double, 3> bbox;
    Transform3f                  trafof = trafo.cast<float>();
    sides.reserve(mesh.its.vertices.size());
    for (const stl_vertex& v : mesh.its.vertices) {
        stl_vertex pt   = trafof * v;
        int        sign = pt.z() > world_z ? 1 : pt.z() < world_z ? -1 : 0;
        sides.emplace_back(sign);
        if (sign >= 0) {
            // Vertex above or on print bed surface. Test whether it is inside the build volume.
            ++ num_above;
            bbox.extend(pt.cast<double>());
        }
    }

    // 2) Calculate intersections of triangle edges with the build surface.
    if (num_above < mesh.its.vertices.size()) {
        // Not completely above the build surface and status may still change by testing edges intersecting the build platform.
        for (const stl_triangle_vertex_indices& tri : mesh.its.indices) {
            int s[3] = { sides[tri[0]], sides[tri[1]], sides[tri[2]] };
            if (std::min({ s[0], s[1], s[2] }) < 0 && std::max({ s[0], s[1], s[2] }) > 0) {
                // Some edge of this triangle intersects the build platform. Calculate the intersection.
                int iprev = 2;
                for (int iedge = 0; iedge < 3; ++ iedge) {
                    if (s[iprev] * s[iedge] == -1) {
                        // Edge crosses the z plane. Calculate intersection point with the plane.
                        stl_vertex p1 = trafof * mesh.its.vertices[tri[iprev]];
                        stl_vertex p2 = trafof * mesh.its.vertices[tri[iedge]];
                        float t = (world_z - p1.z()) / (p2.z() - p1.z());
                        bbox.extend(Vec3d(p1.x() + (p2.x() - p1.x()) * t, p1.y() + (p2.y() - p1.y()) * t, world_z));
                    }
                    iprev = iedge;
                }
            }
        }
    }

    return BoundingBox3d(bbox.min(), bbox.max());
}

TriangleMesh convex_hull_3d(const TriangleMesh& mesh)
{
    using Biz::Algorithms::TriangleMesh::construct;
    TriangleMesh result(construct(its_convex_hull(mesh.its)));
    // Quite often qhull produces non-manifold mesh.
    // assert(mesh.stats().manifold());
    return result;
}

bool is_front_up_left(const TriangleMesh &trinagle_mesh1, const TriangleMesh &triangle_mesh2)
{
    // stats form t1
    const Domain::Vec3f &min1 = trinagle_mesh1.stats().min;
    const Domain::Vec3f &max1 = trinagle_mesh1.stats().max;
    // stats from t2
    const Domain::Vec3f &min2 = triangle_mesh2.stats().min;
    const Domain::Vec3f &max2 = triangle_mesh2.stats().max;
    // priority Z, Y, X
    for (int axe = 2; axe > 0; --axe) {
        if (max1[axe] < min2[axe])
            return true;
        if (min1[axe] > max2[axe])
            return false;
    }
    return min1.x() < min2.x();
}

std::vector<TriangleMesh> split(const TriangleMesh& mesh)
{
    using Biz::Algorithms::TriangleMesh::construct;

    std::vector<indexed_triangle_set> itss = its_split(mesh.its);
    std::vector<TriangleMesh> out;
    out.reserve(itss.size());
    for (indexed_triangle_set &m : itss) {
        // The TriangleMesh constructor shall fill in the mesh statistics including volume.
        out.push_back(construct(std::move(m)));
        if (TriangleMesh &triangle_mesh = out.back(); triangle_mesh.volume() < 0)
            // Some source mesh parts may be incorrectly oriented. Correct them.
            triangle_mesh.flip_triangles();

    }
    return out;
}

bool write_obj_file(const TriangleMesh& mesh, const char* output_file)
{
    return its_write_obj(mesh.its, output_file);
}

// Create a mapping from triangle edge into face.
struct EdgeToFace {
    // Index of the 1st vertex of the triangle edge. vertex_low <= vertex_high.
    int  vertex_low;
    // Index of the 2nd vertex of the triangle edge.
    int  vertex_high;
    // Index of a triangular face.
    int  face;
    // Index of edge in the face, starting with 1. Negative indices if the edge was stored reverse in (vertex_low, vertex_high).
    int  face_edge;
    bool operator==(const EdgeToFace &other) const { return vertex_low == other.vertex_low && vertex_high == other.vertex_high; }
    bool operator<(const EdgeToFace &other) const { return vertex_low < other.vertex_low || (vertex_low == other.vertex_low && vertex_high < other.vertex_high); }
};

template<AdditionalMeshInfo mesh_info = AdditionalMeshInfo::None, typename FaceFilter, typename ThrowOnCancelCallback>
std::vector<EdgeToFace> create_edge_map(const typename IndexedTriangleSetType<mesh_info>::type &its,
                                               FaceFilter                                              face_filter,
                                               ThrowOnCancelCallback                                   throw_on_cancel)
{
    std::vector<EdgeToFace> edges_map;
    edges_map.reserve(its.indices.size() * 3);
    for (uint32_t facet_idx = 0; facet_idx < its.indices.size(); ++ facet_idx)
        if (face_filter(facet_idx))
            for (int i = 0; i < 3; ++ i) {
                edges_map.push_back({});
                EdgeToFace &e2f = edges_map.back();
                e2f.vertex_low  = its.indices[facet_idx][i];
                e2f.vertex_high = its.indices[facet_idx][(i + 1) % 3];
                e2f.face        = facet_idx;
                // 1 based indexing, to be always strictly positive.
                e2f.face_edge   = i + 1;
                if (e2f.vertex_low > e2f.vertex_high) {
                    // Sort the vertices
                    std::swap(e2f.vertex_low, e2f.vertex_high);
                    // and make the face_edge negative to indicate a flipped edge.
                    e2f.face_edge = - e2f.face_edge;
                }
            }
    throw_on_cancel();
    std::sort(edges_map.begin(), edges_map.end());

    return edges_map;
}

// Map from a face edge to a unique edge identifier or -1 if no neighbor exists.
// Two neighbor faces share a unique edge identifier even if they are flipped.
template<AdditionalMeshInfo mesh_info = AdditionalMeshInfo::None, typename FaceFilter, typename ThrowOnCancelCallback>
std::vector<Index3> its_face_edge_ids_impl(const typename IndexedTriangleSetType<mesh_info>::type &its,
                                                        FaceFilter                                              face_filter,
                                                        ThrowOnCancelCallback                                   throw_on_cancel)
{
    std::vector<Index3> out(its.indices.size(), Index3{-1, -1, -1});

    std::vector<EdgeToFace> edges_map = create_edge_map<mesh_info>(its, face_filter, throw_on_cancel);

    // Assign a unique common edge id to touching triangle edges.
    int num_edges = 0;
    for (size_t i = 0; i < edges_map.size(); ++ i) {
        EdgeToFace &edge_i = edges_map[i];
        if (edge_i.face == -1)
            // This edge has been connected to some neighbor already.
            continue;
        // Unconnected edge. Find its neighbor with the correct orientation.
        size_t j;
        bool found = false;
        for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++ j)
            if (edge_i.face_edge * edges_map[j].face_edge < 0 && edges_map[j].face != -1) {
                // Faces touching with opposite oriented edges and none of the edges is connected yet.
                found = true;
                break;
            }
        if (! found) {
            //FIXME Vojtech: Trying to find an edge with equal orientation. This smells.
            // admesh can assign the same edge ID to more than two facets (which is
            // still topologically correct), so we have to search for a duplicate of
            // this edge too in case it was already seen in this orientation
            for (j = i + 1; j < edges_map.size() && edge_i == edges_map[j]; ++ j)
                if (edges_map[j].face != -1) {
                    // Faces touching with equally oriented edges and none of the edges is connected yet.
                    found = true;
                    break;
                }
        }
        // Assign an edge index to the 1st face.
        out[edge_i.face][std::abs(edge_i.face_edge) - 1] = num_edges;
        if (found) {
            EdgeToFace &edge_j = edges_map[j];
            out[edge_j.face][std::abs(edge_j.face_edge) - 1] = num_edges;
            // Mark the edge as connected.
            edge_j.face = -1;
        }
        ++ num_edges;
        if ((i & 0x0ffff) == 0)
            throw_on_cancel();
    }

    return out;
}

// Explicit template instantiation.
template std::vector<Index3> its_face_edge_ids<AdditionalMeshInfo::None>(const IndexedTriangleSetType<AdditionalMeshInfo::None>::type &);
template std::vector<Index3> its_face_edge_ids<AdditionalMeshInfo::Color>(const IndexedTriangleSetType<AdditionalMeshInfo::Color>::type &);
template std::vector<Index3> its_face_edge_ids<AdditionalMeshInfo::None>(const IndexedTriangleSetType<AdditionalMeshInfo::None>::type &, const std::vector<char> &);
template std::vector<Index3> its_face_edge_ids<AdditionalMeshInfo::Color>(const IndexedTriangleSetType<AdditionalMeshInfo::Color>::type &, const std::vector<char> &);

template<AdditionalMeshInfo mesh_info>
std::vector<Index3> its_face_edge_ids(const typename IndexedTriangleSetType<mesh_info>::type &its)
{
    return its_face_edge_ids_impl<mesh_info>(its, [](const uint32_t){ return true; }, [](){});
}

std::vector<Index3> its_face_edge_ids(const indexed_triangle_set &its, std::function<void()> throw_on_cancel_callback)
{
    return its_face_edge_ids_impl(its, [](const uint32_t){ return true; }, throw_on_cancel_callback);
}

template<AdditionalMeshInfo mesh_info>
std::vector<Index3> its_face_edge_ids(const typename IndexedTriangleSetType<mesh_info>::type &its, const std::vector<char> &face_mask)
{
    return its_face_edge_ids_impl<mesh_info>(its, [&face_mask](const uint32_t idx){ return face_mask[idx]; }, [](){});
}

// Having the face neighbors available, assign unique edge IDs to face edges for chaining of polygons over slices.
std::vector<Index3> its_face_edge_ids(const indexed_triangle_set &its, std::vector<Index3> &face_neighbors, bool assign_unbound_edges, int *num_edges)
{
    // out elements are not initialized!
    std::vector<Index3> out(face_neighbors.size());
    int last_edge_id = 0;
    for (int i = 0; i < int(face_neighbors.size()); ++ i) {
        const stl_triangle_vertex_indices   &triangle  = its.indices[i];
        const Index3                         &neighbors = face_neighbors[i];
        for (int j = 0; j < 3; ++ j) {
            int n = neighbors[j];
            if (n > i) {
                const stl_triangle_vertex_indices &triangle2 = its.indices[n];
                int   edge_id = last_edge_id ++;
                Domain::Index2 edge    = its_triangle_edge(triangle, j);
                // First find an edge with opposite orientation.
                std::swap(edge[0], edge[1]);
                int   k       = its_triangle_edge_index(triangle2, edge);
                //FIXME is the following realistic? Could face_neighbors contain such faces?
                // And if it does, do we want to produce the same edge ID for those mutually incorrectly oriented edges?
                if (k == -1) {
                    // Second find an edge with the same orientation (the neighbor triangle may be flipped).
                    std::swap(edge[0], edge[1]);
                    k = its_triangle_edge_index(triangle2, edge);
                }
                assert(k >= 0);
                out[i][j] = edge_id;
                out[n][k] = edge_id;
            } else if (n == -1) {
                out[i][j] = assign_unbound_edges ? last_edge_id ++ : -1;
            } else {
                // Triangle shall never be neighbor of itself.
                assert(n < i);
                // Don't do anything, the neighbor will assign us an edge ID in later iterations.
            }
        }
    }
    if (num_edges)
        *num_edges = last_edge_id;
    return out;
}

// Merge duplicate vertices, return number of vertices removed.
int its_merge_vertices(indexed_triangle_set &its, bool shrink_to_fit)
{
    // 1) Sort indices to vertices lexicographically by coordinates AND vertex index.
    std::vector<int> sorted;
    sorted.reserve(its.vertices.size());
    for (int i = 0; i < int(its.vertices.size()); ++ i)
        sorted.emplace_back(i);
    std::sort(sorted.begin(), sorted.end(), [&its](int il, int ir) {
        const Vec3f &l = its.vertices[il];
        const Vec3f &r = its.vertices[ir];
        // Sort lexicographically by coordinates AND vertex index.
        return l.x() < r.x() || (l.x() == r.x() && (l.y() < r.y() || (l.y() == r.y() && (l.z() < r.z() || (l.z() == r.z() && il < ir)))));
    });

    // 2) Map duplicate vertices to the one with the lowest vertex index.
    // The vertex to stay will have a map_vertices[...] == -1 index assigned, the other vertices will point to it.
    std::vector<int> map_vertices(its.vertices.size(), -1);
    for (int i = 0; i < int(sorted.size());) {
        const int    u = sorted[i];
        const Vec3f &p = its.vertices[u];
        int j = i;
        for (++ j; j < int(sorted.size()); ++ j) {
            const int    v = sorted[j];
            const Vec3f &q = its.vertices[v];
            if (p != q)
                break;
            assert(v > u);
            map_vertices[v] = u;
        }
        i = j;
    }

    // 3) Shrink its.vertices, update map_vertices with the new vertex indices.
    int k = 0;
    for (int i = 0; i < int(its.vertices.size()); ++ i) {
        if (map_vertices[i] == -1) {
            map_vertices[i] = k;
            if (k < i)
                its.vertices[k] = its.vertices[i];
            ++ k;
        } else {
            assert(map_vertices[i] < i);
            map_vertices[i] = map_vertices[map_vertices[i]];
        }
    }

    int num_erased = int(its.vertices.size()) - k;

    if (num_erased) {
        // Shrink the vertices.
        its.vertices.erase(its.vertices.begin() + k, its.vertices.end());
        // Remap face indices.
        for (stl_triangle_vertex_indices &face : its.indices)
            for (int i = 0; i < 3; ++ i)
                face[i] = map_vertices[face[i]];
        // Optionally shrink to fit (reallocate) vertices.
        if (shrink_to_fit)
            its.vertices.shrink_to_fit();
    }

    return num_erased;
}

int its_num_degenerate_faces(const indexed_triangle_set &its)
{
    return std::count_if(its.indices.begin(), its.indices.end(), [](auto &face) {
        return face[0] == face[1] || face[0] == face[2] || face[1] == face[2];
    });
}

int its_remove_degenerate_faces(indexed_triangle_set &its, bool shrink_to_fit)
{
    auto it = std::remove_if(its.indices.begin(), its.indices.end(), [](auto &face) {
        return face[0] == face[1] || face[0] == face[2] || face[1] == face[2];
    });

    int removed = std::distance(it, its.indices.end());
    its.indices.erase(it, its.indices.end());

    if (removed && shrink_to_fit)
        its.indices.shrink_to_fit();

    return removed;
}

int its_compactify_vertices(indexed_triangle_set &its, bool shrink_to_fit)
{
    // First used to mark referenced vertices, later used for mapping old vertex index to a new one.
    std::vector<int> vertex_map(its.vertices.size(), 0);
    // Mark referenced vertices.
    for (const stl_triangle_vertex_indices &face : its.indices)
        for (int i = 0; i < 3; ++ i)
            vertex_map[face[i]] = 1;
    // Compactify vertices, update map from old vertex index to a new one.
    int last = 0;
    for (int i = 0; i < int(vertex_map.size()); ++ i)
        if (vertex_map[i]) {
            if (last < i)
                its.vertices[last] = its.vertices[i];
            vertex_map[i] = last ++;
        }
    int removed = int(its.vertices.size()) - last;
    if (removed) {
        its.vertices.erase(its.vertices.begin() + last, its.vertices.end());
        // Update faces with the new vertex indices.
        for (stl_triangle_vertex_indices &face : its.indices)
            for (int i = 0; i < 3; ++ i)
                face[i] = vertex_map[face[i]];
        // Optionally shrink the vertices.
        if (shrink_to_fit)
            its.vertices.shrink_to_fit();
    }
    return removed;
}

bool its_store_triangle_to_obj(const indexed_triangle_set &its,
                               const char                 *obj_filename,
                               size_t                      triangle_index)
{
    if (its.indices.size() <= triangle_index) return false;
    Index3                t = its.indices[triangle_index];
    indexed_triangle_set its2;
    its2.indices  = {{0, 1, 2}};
    its2.vertices = {its.vertices[t[0]], its.vertices[t[1]],
                     its.vertices[t[2]]};
    return its_write_obj(its2, obj_filename);
}

bool its_store_triangles_to_obj(const indexed_triangle_set &its,
                                const char                 *obj_filename,
                                const std::vector<size_t>  &triangles)
{
    indexed_triangle_set its2;
    its2.vertices.reserve(triangles.size() * 3);
    its2.indices.reserve(triangles.size());
    std::map<size_t, size_t> vertex_map;
    for (auto ti : triangles) {
        if (its.indices.size() <= ti) return false;
        Index3 t = its.indices[ti];
        Index3 new_t;
        for (size_t i = 0; i < 3; ++i) {
            size_t vi = t[i];
            auto   it = vertex_map.find(vi);
            if (it != vertex_map.end()) {
                new_t[i] = it->second;
                continue;
            }
            size_t new_vi = its2.vertices.size();
            its2.vertices.push_back(its.vertices[vi]);
            vertex_map[vi] = new_vi;
            new_t[i]       = new_vi;
        }
        its2.indices.push_back(new_t);
    }
    return its_write_obj(its2, obj_filename);
}

void its_shrink_to_fit(indexed_triangle_set &its)
{
    its.indices.shrink_to_fit();
    its.vertices.shrink_to_fit();
}

template<typename TransformVertex>
void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const TransformVertex &transform_fn, const float z, Points &all_pts)
{
    all_pts.reserve(all_pts.size() + its.indices.size() * 3);
    for (const stl_triangle_vertex_indices &tri : its.indices) {
        const Vec3f pts[3] = { transform_fn(its.vertices[tri[0]]), transform_fn(its.vertices[tri[1]]), transform_fn(its.vertices[tri[2]]) };
        int iprev = 2;
        for (int iedge = 0; iedge < 3; ++ iedge) {
            const Vec3f &p1 = pts[iprev];
            const Vec3f &p2 = pts[iedge];
            if ((p1.z() < z && p2.z() > z) || (p2.z() < z && p1.z() > z)) {
                // Edge crosses the z plane. Calculate intersection point with the plane.
                float t = (z - p1.z()) / (p2.z() - p1.z());
                all_pts.emplace_back(scaled<coord_t>(p1.x() + (p2.x() - p1.x()) * t), scaled<coord_t>(p1.y() + (p2.y() - p1.y()) * t));
            }
            if (p2.z() >= z)
                all_pts.emplace_back(scaled<coord_t>(p2.x()), scaled<coord_t>(p2.y()));
            iprev = iedge;
        }
    }
}

void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const SquareMatrix3f &m, const float z, Points &all_pts)
{
    return its_collect_mesh_projection_points_above(its, [m](const Vec3f &p){ return m * p; }, z, all_pts);
}

void its_collect_mesh_projection_points_above(const indexed_triangle_set &its, const Transform3f &t, const float z, Points &all_pts)
{
    return its_collect_mesh_projection_points_above(its, [t](const Vec3f &p){ return t * p; }, z, all_pts);
}

template<typename TransformVertex>
Polygon its_convex_hull_2d_above(const indexed_triangle_set& its, const TransformVertex& transform_fn, const float z)
{
    auto collect_mesh_projection_points_above = [&](const tbb::blocked_range<size_t>& range) {
        Points pts;
        pts.reserve(range.size() * 4); // there can be up to 4 vertices per triangle
        for (size_t i = range.begin(); i < range.end(); ++i) {
            const stl_triangle_vertex_indices& tri = its.indices[i];
            const Vec3f tri_pts[3] = { transform_fn(its.vertices[tri[0]]), transform_fn(its.vertices[tri[1]]), transform_fn(its.vertices[tri[2]]) };
            int iprev = 2;
            for (int iedge = 0; iedge < 3; ++iedge) {
                const Vec3f& p1 = tri_pts[iprev];
                const Vec3f& p2 = tri_pts[iedge];
                if ((p1.z() < z && p2.z() > z) || (p2.z() < z && p1.z() > z)) {
                    // Edge crosses the z plane. Calculate intersection point with the plane.
                    const float t = (z - p1.z()) / (p2.z() - p1.z());
                    pts.emplace_back(scaled<coord_t>(p1.x() + (p2.x() - p1.x()) * t), scaled<coord_t>(p1.y() + (p2.y() - p1.y()) * t));
                }
                if (p2.z() >= z)
                    pts.emplace_back(scaled<coord_t>(p2.x()), scaled<coord_t>(p2.y()));
                iprev = iedge;
            }
        }
        using Biz::Algorithms::Geometry::convex_hull;
        return convex_hull(std::move(pts));
    };

    tbb::concurrent_vector<Polygon> chs;
    tbb::parallel_for(tbb::blocked_range<size_t>(0, its.indices.size()), [&](const tbb::blocked_range<size_t>& range) {
        chs.push_back(collect_mesh_projection_points_above(range));
    });

    const Polygons polygons(std::make_move_iterator(chs.begin()), std::make_move_iterator(chs.end()));
    using Biz::Algorithms::Geometry::convex_hull;
    return convex_hull(polygons);
}

Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const SquareMatrix3f &m, const float z)
{
    return its_convex_hull_2d_above(its, [m](const Vec3f &p){ return m * p; }, z);
}

Polygon its_convex_hull_2d_above(const indexed_triangle_set &its, const Transform3f &t, const float z)
{
    return its_convex_hull_2d_above(its, [t](const Vec3f &p){ return t * p; }, z);
}

indexed_triangle_set its_make_tetrahedron(float size) {
    // --- Calculations for a "flat-base" regular tetrahedron ---
    // 'size' is the edge length.

    // 1. Calculate the circumradius 'r' of the base equilateral triangle
    // This is the distance from the center of the base to its vertices.
    const float sqrt_3 = static_cast<float>(std::sqrt(3.0));
    const float sqrt_23 = static_cast<float>(std::sqrt(2/3.0));

    const float r = size / sqrt_3;

    // 2. Calculate the total height 'h' of the tetrahedron
    const float h = size * sqrt_23;

    // 3. Calculate the Y coordinates for the top and base
    // The centroid (origin) is 1/4 of the way up from the base.
    const float y_base = -h / 4.0f;
    const float y_top = h * 3.0f / 4.0f;

    // 4. Calculate the XZ coordinates for the 3 base vertices
    const float v1x = 0.0f;
    const float v1z = r;

    const float v2x = r * sqrt_3 / 2.0f; // r * sin(120 deg)
    const float v2z = -r / 2.0f;         // r * cos(120 deg)

    const float v3x = -r * sqrt_3 / 2.0f; // r * sin(240 deg)
    const float v3z = -r / 2.0f;          // r * cos(240 deg)

    return {{  // indices
        // Base face (facing -Y)
        {3, 2, 1},
        // Side face 1
        {0, 1, 2},
        // Side face 2
        {0, 2, 3},
        // Side face 3
        {0, 3, 1}
    }, { // vertices
        // Vertex 0 (Top)
        {0.0f, y_top,  0.0f},
         // Vertex 1 (Base)
        {v1x,  y_base, v1z},
          // Vertex 2 (Base)
        {v2x,  y_base, v2z},
           // Vertex 3 (Base)
        {v3x,  y_base, v3z}
    }};
}

// Generate the vertex list for a cube solid of arbitrary size in X/Y/Z.
indexed_triangle_set its_make_cube(double xd, double yd, double zd)
{
    auto x = float(xd), y = float(yd), z = float(zd);
    return {
        { {0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7},
          {0, 4, 7}, {0, 7, 1}, {1, 7, 6}, {1, 6, 2},
          {2, 6, 5}, {2, 5, 3}, {4, 0, 3}, {4, 3, 5} },
        { {x, y, 0}, {x, 0, 0}, {0, 0, 0}, {0, y, 0},
          {x, y, z}, {0, y, z}, {0, 0, z}, {x, 0, z} }
    };
}

indexed_triangle_set its_make_prism(float width, float length, float height)
{
    // We need two upward facing triangles
    float x = width / 2.f, y = length / 2.f;
    return {
        {
            {0, 1, 2}, // side 1
            {4, 3, 5}, // side 2
            {1, 4, 2}, {2, 4, 5}, // roof 1
            {0, 2, 5}, {0, 5, 3}, // roof 2
            {3, 4, 1}, {3, 1, 0} // bottom
        },
        {
            {-x, -y, 0.f}, {x, -y, 0.f}, {0.f, -y, height},
            {-x, y, 0.f}, {x, y, 0.f}, {0.f, y, height},
        }
    };
}

// Generate the mesh for a cylinder and return it, using 
// the generated angle to calculate the top mesh triangles.
// Default is 360 sides, angle fa is in radians.
indexed_triangle_set its_make_cylinder(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    size_t n_steps    = (size_t)ceil(2. * std::numbers::pi / fa);
    double angle_step = 2. * std::numbers::pi / n_steps;

    auto &vertices = mesh.vertices;
    auto &facets   = mesh.indices;
    vertices.reserve(2 * n_steps + 2);
    facets.reserve(4 * n_steps);

    // 2 special vertices, top and bottom center, rest are relative to this
    vertices.emplace_back(Vec3f(0.f, 0.f, 0.f));
    vertices.emplace_back(Vec3f(0.f, 0.f, float(h)));

    // for each line along the polygon approximating the top/bottom of the
    // circle, generate four points and four facets (2 for the wall, 2 for the
    // top and bottom.
    // Special case: Last line shares 2 vertices with the first line.
    Vec2f p = Eigen::Rotation2Df(0.f) * Eigen::Vector2f(0, r);
    vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
    vertices.emplace_back(Vec3f(p(0), p(1), float(h)));
    for (size_t i = 1; i < n_steps; ++i) {
        p = Eigen::Rotation2Df(angle_step * i) * Eigen::Vector2f(0, float(r));
        vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
        vertices.emplace_back(Vec3f(p(0), p(1), float(h)));
        int id = (int)vertices.size() - 1;
        facets.emplace_back(Index3{0, id - 1, id - 3}); // top
        facets.emplace_back(Index3{id,      1, id - 2}); // bottom
        facets.emplace_back(Index3{id, id - 2, id - 3}); // upper-right of side
        facets.emplace_back(Index3{id, id - 3, id - 1}); // bottom-left of side
    }
    // Connect the last set of vertices with the first.
    int id = (int)vertices.size() - 1;
    facets.emplace_back(Index3{ 0, 2, id - 1});
    facets.emplace_back(Index3{ 3, 1,     id});
    facets.emplace_back(Index3{id, 2,      3});
    facets.emplace_back(Index3{id, id - 1, 2});

    return mesh;
}

indexed_triangle_set its_make_frustum(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    size_t n_steps    = (size_t)ceil(2. * std::numbers::pi / fa);
    double angle_step = 2. * std::numbers::pi / n_steps;

    auto &vertices = mesh.vertices;
    auto &facets   = mesh.indices;
    vertices.reserve(2 * n_steps + 2);
    facets.reserve(4 * n_steps);

    // 2 special vertices, top and bottom center, rest are relative to this
    vertices.emplace_back(Vec3f(0.f, 0.f, 0.f));
    vertices.emplace_back(Vec3f(0.f, 0.f, float(h)));

    // for each line along the polygon approximating the top/bottom of the
    // circle, generate four points and four facets (2 for the wall, 2 for the
    // top and bottom.
    // Special case: Last line shares 2 vertices with the first line.
    Vec2f vec_top = Eigen::Rotation2Df(0.f) * Eigen::Vector2f(0, 0.5f*r);
    Vec2f vec_botton = Eigen::Rotation2Df(0.f) * Eigen::Vector2f(0, r);

    vertices.emplace_back(Vec3f(vec_botton(0), vec_botton(1), 0.f));
    vertices.emplace_back(Vec3f(vec_top(0), vec_top(1), float(h)));
    for (size_t i = 1; i < n_steps; ++i) {
        vec_top = Eigen::Rotation2Df(angle_step * i) * Eigen::Vector2f(0, 0.5f*float(r));
        vec_botton = Eigen::Rotation2Df(angle_step * i) * Eigen::Vector2f(0, float(r));
        vertices.emplace_back(Vec3f(vec_botton(0), vec_botton(1), 0.f));
        vertices.emplace_back(Vec3f(vec_top(0), vec_top(1), float(h)));
        int id = (int)vertices.size() - 1;
        facets.emplace_back(Index3{ 0, id - 1, id - 3}); // top
        facets.emplace_back(Index3{id,      1, id - 2}); // bottom
        facets.emplace_back(Index3{id, id - 2, id - 3}); // upper-right of side
        facets.emplace_back(Index3{id, id - 3, id - 1}); // bottom-left of side
    }
    // Connect the last set of vertices with the first.
    int id = (int)vertices.size() - 1;
    facets.emplace_back(Index3{ 0, 2, id - 1});
    facets.emplace_back(Index3{ 3, 1,     id});
    facets.emplace_back(Index3{id, 2,      3});
    facets.emplace_back(Index3{id, id - 1, 2});

    return mesh;
}

indexed_triangle_set its_make_cone(double r, double h, double fa)
{
    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    auto& facets = mesh.indices;
    vertices.reserve(3 + 2 * size_t(2 * std::numbers::pi / fa));

    // base center and top vertex
    vertices.emplace_back(Vec3f::Zero());
    vertices.emplace_back(Vec3f(0., 0., h));

    size_t i = 0;
    const auto vec = Eigen::Vector2f(0, float(r));
    for (double angle=0; angle<2*std::numbers::pi; angle+=fa) {
        Vec2f p = Eigen::Rotation2Df(angle) * vec;
        vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
        if (angle > 0.) {
            facets.emplace_back(Index3{
                0,
                static_cast<int>(i+2),
                static_cast<int>(i+1)
            });
            facets.emplace_back(Index3{
                1,
                static_cast<int>(i+1),
                static_cast<int>(i+2)
            });
        }
        ++i;
    }
    facets.emplace_back(Index3{ 0, 2, static_cast<int>(i+1) });
    // close the shape
    facets.emplace_back(Index3{1, static_cast<int>(i+1), 2});

    return mesh;
}

indexed_triangle_set its_make_pyramid(float base, float height)
{
    float a = base / 2.f;
    return {
        {
            {0, 1, 2},
            {0, 2, 3},
            {0, 1, 4},
            {1, 2, 4},
            {2, 3, 4},
            {3, 0, 4}
        },
        {
            {-a, -a, 0}, {a, -a, 0}, {a, a, 0},
            {-a, a, 0}, {0.f, 0.f, height}
        }
    };
}

// Generates mesh for a sphere centered about the origin, using the generated angle
// to determine the granularity. 
// Default angle is 1 degree.
indexed_triangle_set its_make_sphere(double radius, double fa)
{
    // First build an icosahedron (taken from http://www.songho.ca/opengl/gl_sphere.html)
    indexed_triangle_set mesh;

    const float PI = 3.1415926f;
    const float H_ANGLE = PI / 180 * 72;    // 72 degree = 360 / 5
    const float V_ANGLE = atanf(1.0f / 2);  // elevation = 26.565 degree

    auto& vertices = mesh.vertices;
    auto& indices = mesh.indices;
    vertices.resize(12);
    indices.reserve(20);

    float z, xy;
    float hAngle1 = -PI / 2 - H_ANGLE / 2;

    vertices[0] = stl_vertex(0, 0, radius); // the first top vertex at (0, 0, r)

    for (int i = 1; i <= 5; ++i) {
        z  = radius * sinf(V_ANGLE);
        xy = radius * cosf(V_ANGLE);
        vertices[i] = stl_vertex(xy * cosf(hAngle1), xy * sinf(hAngle1), z);
        vertices[i+5] = stl_vertex(xy * cosf(hAngle1 + H_ANGLE / 2), xy * sinf(hAngle1 + H_ANGLE / 2), -z);
        hAngle1 += H_ANGLE;

        indices.emplace_back(stl_triangle_vertex_indices{i, i < 5 ? i+1 : 1, 0});
        indices.emplace_back(stl_triangle_vertex_indices{i, i+5, i < 5 ? i+1 : 1});
        indices.emplace_back(stl_triangle_vertex_indices{i+5, i+6 < 11 ? i+6 : 6, i+6 < 11 ? i+1 : 1});
        indices.emplace_back(stl_triangle_vertex_indices{i+5, 11, i+6 < 11 ? i+6 : 6});
    }
    vertices[11] = stl_vertex(0, 0, -radius); // the last bottom vertex at (0, 0, -r)

    
    // We have a beautiful icosahedron. Now subdivide the triangles.
    std::vector<Index3> neighbors = its_face_neighbors(mesh); // This is cheap, the mesh is small.

    const double side_len_limit = radius * fa;
    const double side_len = (vertices[1] - vertices[0]).norm();
    const int iterations = std::ceil(std::log2(side_len / side_len_limit));

    indices.reserve(indices.size() * std::pow(4, iterations));
    vertices.reserve(vertices.size() * std::pow(2, iterations));

    struct DividedEdge {
        int neighbor = -1;
        int middle_vertex_idx;
        std::pair<int, int> children_idxs;
    };

    for (int iter=0; iter<iterations; ++iter) {
        std::vector<std::array<DividedEdge, 3>> divided_triangles(indices.size());
        std::vector<Index3> new_neighbors(4*indices.size());

        int orig_indices_size = int(indices.size());
        for (int i=0; i<orig_indices_size; ++i) { // iterate over all old triangles

            // We are going to split this triangle. Let's foresee what will be the indices
            // of the new internal triangles along individual edges.
            int last_triangle_idx = indices.size()-1;
            std::array<std::pair<int, int>, 3> edge_children = { std::make_pair(i,last_triangle_idx + 2),
                                                                 std::make_pair(last_triangle_idx + 2,last_triangle_idx + 3),
                                                                 std::make_pair(last_triangle_idx + 3,i) };

            std::array<int, 3> middle_vertices_idxs;
            std::array<std::pair<int, int>, 3> new_neighbors_per_edge;

            for (int n=0; n<3; ++n) { // for all three edges
                const int edge_neighbor = neighbors[i][n];

                if (divided_triangles[edge_neighbor][0].neighbor == -1) {
                    // This n-th edge is not yet divided. Divide it now.
                    vertices.emplace_back(0.5 * (vertices[indices[i][n]] + vertices[indices[i][n == 2 ? 0 : n+1]]));
                    vertices.back() *= radius / vertices.back().norm();
                    middle_vertices_idxs[n] = vertices.size()-1;

                    // Save information about what we did.
                    int j = -1;
                    while (divided_triangles[i][++j].neighbor != -1);
                    
                    divided_triangles[i][j] = { edge_neighbor, int(vertices.size()-1), edge_children[n] };
                    new_neighbors_per_edge[n] = std::make_pair(-1,-1);
                } else {
                    // This edge is already divided. Get the index of the middle point.
                    int j = -1;
                    while (divided_triangles[edge_neighbor][++j].neighbor != i);
                    middle_vertices_idxs[n] = divided_triangles[edge_neighbor][j].middle_vertex_idx;
                    new_neighbors_per_edge[n] = divided_triangles[edge_neighbor][j].children_idxs;
                    std::swap(new_neighbors_per_edge[n].first, new_neighbors_per_edge[n].second);

                    // We have saved the middle-point. We are looking for edges leading to/from it.
                    int idx = -1; while (indices[new_neighbors_per_edge[n].first][++idx] != middle_vertices_idxs[n]);
                    new_neighbors[new_neighbors_per_edge[n].first][idx] = edge_children[n].first;
                    new_neighbors[new_neighbors_per_edge[n].second][idx] = edge_children[n].second;
                }
            }

            // Add three new triangles, reindex the old one.
            const int last_index = indices.size() - 1;
            indices.emplace_back(stl_triangle_vertex_indices{middle_vertices_idxs[0], middle_vertices_idxs[1], middle_vertices_idxs[2]});
            new_neighbors[indices.size()-1] = Index3{last_index+2, last_index+3, i};

            indices.emplace_back(stl_triangle_vertex_indices{middle_vertices_idxs[0], indices[i][1], middle_vertices_idxs[1]});
            new_neighbors[indices.size()-1] = Index3{new_neighbors_per_edge[0].second, new_neighbors_per_edge[1].first, last_index+1};

            indices.emplace_back(stl_triangle_vertex_indices{middle_vertices_idxs[2], middle_vertices_idxs[1], indices[i][2]});
            new_neighbors[indices.size()-1] = Index3{last_index+1, new_neighbors_per_edge[1].second, new_neighbors_per_edge[2].first};

            indices[i][1] = middle_vertices_idxs[0];
            indices[i][2] = middle_vertices_idxs[2];
            new_neighbors[i] = Index3{new_neighbors_per_edge[0].first, last_index+1, new_neighbors_per_edge[2].second};

        }
        neighbors = std::move(new_neighbors);
    }
    return mesh;
}

// Generates mesh for a frustum dowel centered about the origin, using the count of sectors
// Note: This function uses code for sphere generation, but for stackCount = 2;
indexed_triangle_set its_make_frustum_dowel(double radius, double h, int sectorCount)
{
    int   stackCount = 2;
    float sectorStep = float(2. * M_PI / sectorCount);
    float stackStep = float(M_PI / stackCount);

    indexed_triangle_set mesh;
    auto& vertices = mesh.vertices;
    vertices.reserve((stackCount - 1) * sectorCount + 2);
    for (int i = 0; i <= stackCount; ++i) {
        // from pi/2 to -pi/2
        double stackAngle = 0.5 * M_PI - stackStep * i;
        double xy = radius * cos(stackAngle);
        double z = radius * sin(stackAngle);
        if (i == 0 || i == stackCount)
            vertices.emplace_back(Vec3f(float(xy), 0.f, float(h * sin(stackAngle))));
        else
            for (int j = 0; j < sectorCount; ++j) {
                // from 0 to 2pi
                double sectorAngle = sectorStep * j + 0.25 * M_PI;
                vertices.emplace_back(Vec3d(xy * std::cos(sectorAngle), xy * std::sin(sectorAngle), z).cast<float>());
            }
    }

    auto& facets = mesh.indices;
    facets.reserve(2 * (stackCount - 1) * sectorCount);
    for (int i = 0; i < stackCount; ++i) {
        // Beginning of current stack.
        int k1 = (i == 0) ? 0 : (1 + (i - 1) * sectorCount);
        int k1_first = k1;
        // Beginning of next stack.
        int k2 = (i == 0) ? 1 : (k1 + sectorCount);
        int k2_first = k2;
        for (int j = 0; j < sectorCount; ++j) {
            // 2 triangles per sector excluding first and last stacks
            int k1_next = k1;
            int k2_next = k2;
            if (i != 0) {
                k1_next = (j + 1 == sectorCount) ? k1_first : (k1 + 1);
                facets.emplace_back(Index3{k1, k2, k1_next});
            }
            if (i + 1 != stackCount) {
                k2_next = (j + 1 == sectorCount) ? k2_first : (k2 + 1);
                facets.emplace_back(Index3{k1_next, k2, k2_next});
            }
            k1 = k1_next;
            k2 = k2_next;
        }
    }

    return mesh;
}

indexed_triangle_set its_make_snap(double r, double h, float space_proportion, float bulge_proportion)
{
    const float radius = (float)r;
    const float height = (float)h;
    const size_t sectors_cnt = 10; //(float)fa;
    const float halfPI = 0.5f * (float)std::numbers::pi;

    const float space_len = space_proportion * radius;

    const float b_len = radius;
    const float m_len = (1 + bulge_proportion) * radius;
    const float t_len = 0.5f * radius;

    const float b_height = 0.f;
    const float m_height = 0.5f * height;
    const float t_height = height;

    const float b_angle = acos(space_len/b_len);
    const float t_angle = acos(space_len/t_len);

    const float b_angle_step = b_angle / (float)sectors_cnt;
    const float t_angle_step = t_angle / (float)sectors_cnt;

    const Vec2f b_vec = Eigen::Vector2f(0, b_len);
    const Vec2f t_vec = Eigen::Vector2f(0, t_len);


    auto add_side_vertices = [b_vec, t_vec, b_height, m_height, t_height](std::vector<stl_vertex>& vertices, float b_angle, float t_angle, const Vec2f& m_vec) {
        Vec2f b_pt = Eigen::Rotation2Df(b_angle) * b_vec;
        Vec2f m_pt = Eigen::Rotation2Df(b_angle) * m_vec;
        Vec2f t_pt = Eigen::Rotation2Df(t_angle) * t_vec;

        vertices.emplace_back(Vec3f(b_pt(0), b_pt(1), b_height));
        vertices.emplace_back(Vec3f(m_pt(0), m_pt(1), m_height));
        vertices.emplace_back(Vec3f(t_pt(0), t_pt(1), t_height));
    };

    auto add_side_facets = [](std::vector<stl_triangle_vertex_indices>& facets, int vertices_cnt, int frst_id, int scnd_id) {
        int id = vertices_cnt - 1;

        facets.emplace_back(Index3{frst_id, id - 2, id - 5});

        facets.emplace_back(Index3{id - 2, id - 1, id - 5});
        facets.emplace_back(Index3{id - 1, id - 4, id - 5});
        facets.emplace_back(Index3{id - 4, id - 1, id});
        facets.emplace_back(Index3{id, id - 3, id - 4});

        facets.emplace_back(Index3{id, scnd_id, id - 3});
    };

    const float f = (b_len - m_len) / m_len; // Flattening

    auto get_m_len = [b_len, f](float angle) {
        const float rad_sqr = b_len * b_len;
        const float sin_sqr = sin(angle) * sin(angle);
        const float f_sqr = (1-f)*(1-f);
        return sqrtf(rad_sqr / (1 + (1 / f_sqr - 1) * sin_sqr));
    };

    auto add_sub_mesh = [add_side_vertices, add_side_facets, get_m_len,
                        b_height, t_height, b_angle, t_angle, b_angle_step, t_angle_step]
                        (indexed_triangle_set& mesh, float center_x, float angle_rotation, int frst_vertex_id) {
        auto& vertices = mesh.vertices;
        auto& facets     = mesh.indices;

        // 2 special vertices, top and bottom center, rest are relative to this
        vertices.emplace_back(Vec3f(center_x, 0.f, b_height));
        vertices.emplace_back(Vec3f(center_x, 0.f, t_height));

        float b_angle_start = angle_rotation - b_angle;
        float t_angle_start = angle_rotation - t_angle;
        const float b_angle_stop  = angle_rotation + b_angle;

        const int frst_id = frst_vertex_id;
        const int scnd_id = frst_id + 1;

        // add first side vertices and internal facets
        {
            const Vec2f m_vec = Eigen::Vector2f(0, get_m_len(b_angle_start));
            add_side_vertices(vertices, b_angle_start, t_angle_start, m_vec);

            int id = (int)vertices.size() - 1;

            facets.emplace_back(Index3{frst_id, id - 2, id - 1});
            facets.emplace_back(Index3{frst_id, id - 1, id});
            facets.emplace_back(Index3{frst_id, id, scnd_id});
        }

        // add d side vertices and facets
        while (!Domain::is_approx(b_angle_start, b_angle_stop)) {
            b_angle_start += b_angle_step;
            t_angle_start += t_angle_step;

            const Vec2f m_vec = Eigen::Vector2f(0, get_m_len(b_angle_start));
            add_side_vertices(vertices, b_angle_start, t_angle_start, m_vec);

            add_side_facets(facets, (int)vertices.size(), frst_id, scnd_id);
        }

        // add last internal facets to close the mesh
        {
            int id = (int)vertices.size() - 1;

            facets.emplace_back(Index3{frst_id, scnd_id, id});
            facets.emplace_back(Index3{frst_id, id, id - 1});
            facets.emplace_back(Index3{frst_id, id - 1, id - 2});
        }
    };


    indexed_triangle_set mesh;

    mesh.vertices.reserve(2 * (3 * (2 * sectors_cnt + 1) + 2));
    mesh.indices.reserve(2 * (6 * 2 * sectors_cnt + 6));

    add_sub_mesh(mesh, -space_len, halfPI    , 0);
    add_sub_mesh(mesh,  space_len, 3 * halfPI, (int)mesh.vertices.size());

    return mesh;
}

// Generates mesh for a torus centered about the origin, laying in the XY plane, with the given radius
// and tickness and using the generated angles to determine the granularity. 
// Default angles are 1 degree.
indexed_triangle_set its_make_torus(double r, double t, double ra, double ta)
{
    indexed_triangle_set mesh;

    size_t n_main_steps   = (size_t) ceil(2.0 * std::numbers::pi / ra);
    float main_angle_step = 2.0f * std::numbers::pi / n_main_steps;

    size_t n_secondary_steps   = (size_t) ceil(2.0 * std::numbers::pi / ta);
    float secondary_angle_step = 2.0f * std::numbers::pi / n_secondary_steps;

    mesh.vertices.reserve(n_main_steps * n_secondary_steps);
    mesh.indices.reserve(2 * n_main_steps * n_secondary_steps);

    // vertices
    for (size_t i = 0; i < n_main_steps; ++i) {
        float section_angle = main_angle_step * i;
        Vec3f radius_dir(cosf(section_angle), sinf(section_angle), 0.0f);
        Vec3f section_center = r * radius_dir;
        Vec3f section_normal = section_center.normalized().cross(Vec3f::UnitZ()).normalized();
        Vec3f base_v         = t * radius_dir;
        for (size_t j = 0; j < n_secondary_steps; ++j) {
            mesh.vertices.emplace_back(
                section_center + Eigen::AngleAxisf(secondary_angle_step * j, section_normal) * base_v
            );
        }
    }

    // indices
    for (size_t i = 0; i < n_main_steps; ++i) {
        size_t ii      = i * n_secondary_steps;
        size_t ii_next = ((i + 1) % n_main_steps) * n_secondary_steps;
        for (size_t j = 0; j < n_secondary_steps; ++j) {
            size_t j_next   = (j + 1) % n_secondary_steps;
            size_t i0       = ii + j;
            size_t i1       = ii_next + j;
            size_t i2       = ii_next + j_next;
            size_t i3       = ii + j_next;
            Index3 triangle = {int(i0), int(i1), int(i2)};
            mesh.indices.emplace_back(triangle);
            triangle = {int(i0), int(i2), int(i3)};
            mesh.indices.emplace_back(triangle);
        }
    }

    return mesh;
}

indexed_triangle_set its_convex_hull(const std::vector<Vec3f> &pts)
{
    std::vector<Vec3f>  dst_vertices;
    std::vector<Index3>  dst_facets;

    if (! pts.empty()) {
        // The qhull call:
        orgQhull::Qhull qhull;
        qhull.disableOutputStream(); // we want qhull to be quiet
    #if ! REALfloat
        std::vector<realT> src_vertices;
    #endif
        try {
    #if REALfloat
            qhull.runQhull("", 3, (int)pts.size(), (const realT*)(pts.front().data()), "Qt");
    #else
            src_vertices.reserve(pts.size() * 3);
            // We will now fill the vector with input points for computation:
            for (const stl_vertex &v : pts)
                for (int i = 0; i < 3; ++ i)
                    src_vertices.emplace_back(v(i));
            qhull.runQhull("", 3, (int)src_vertices.size() / 3, src_vertices.data(), "Qt");
    #endif
        } catch (...) {
            SPDLOG_ERROR("its_convex_hull: Unable to create convex hull");
            return {};
        }

        // Let's collect results:
        // Map of QHull's vertex ID to our own vertex ID (pointing to dst_vertices).
        std::vector<int>    map_dst_vertices;
    #ifndef NDEBUG
        Vec3f               centroid = Vec3f::Zero();
        for (const stl_vertex& pt : pts)
            centroid += pt;
        centroid /= float(pts.size());
    #endif // NDEBUG
        for (const orgQhull::QhullFacet &facet : qhull.facetList()) {
            // Collect face vertices first, allocate unique vertices in dst_vertices based on QHull's vertex ID.
            Index3  indices;
            int    cnt = 0;
            for (const orgQhull::QhullVertex vertex : facet.vertices()) {
                int id = vertex.id();
                assert(id >= 0);
                if (id >= int(map_dst_vertices.size()))
                    map_dst_vertices.resize(next_highest_power_of_2(size_t(id + 1)), -1);
                if (int i = map_dst_vertices[id]; i == -1) {
                    // Allocate a new vertex.
                    i = int(dst_vertices.size());
                    map_dst_vertices[id] = i;
                    orgQhull::QhullPoint pt(vertex.point());
                    dst_vertices.emplace_back(pt[0], pt[1], pt[2]);
                    indices[cnt] = i;
                } else {
                    // Reuse existing vertex.
                    indices[cnt] = i;
                }
                if (cnt ++ == 3)
                    break;
            }
            assert(cnt == 3);
            if (cnt == 3) {
                // QHull sorts vertices of a face lexicographically by their IDs, not by face normals.
                // Calculate face normal based on the order of vertices.
                Vec3f n  = (dst_vertices[indices[1]] - dst_vertices[indices[0]]).cross(dst_vertices[indices[2]] - dst_vertices[indices[1]]);
                auto *n2 = facet.getBaseT()->normal;
                auto  d = n.x() * n2[0] + n.y() * n2[1] + n.z() * n2[2];
    #ifndef NDEBUG
                Vec3f n3 = (dst_vertices[indices[0]] - centroid);
                auto  d3 = n.dot(n3);
                assert((d < 0.f) == (d3 < 0.f));
    #endif // NDEBUG
                // Get the face normal from QHull.
                if (d < 0.f)
                    // Fix face orientation.
                    std::swap(indices[1], indices[2]);
                dst_facets.emplace_back(indices);
            }
        }
    }

    return { std::move(dst_facets), std::move(dst_vertices) };
}

void its_reverse_all_facets(indexed_triangle_set &its)
{
    for (stl_triangle_vertex_indices &face : its.indices)
        std::swap(face[0], face[1]);
}

float its_average_edge_length(const indexed_triangle_set &its)
{
    if (its.indices.empty())
        return 0.f;

    double edge_length = 0.f;
    for (size_t i = 0; i < its.indices.size(); ++ i) {
        const Domain::its_triangle v = Domain::its_triangle_vertices(its, i);
        edge_length += (v[1] - v[0]).cast<double>().norm() + 
                       (v[2] - v[0]).cast<double>().norm() +
                       (v[1] - v[2]).cast<double>().norm();
    }
    return float(edge_length / (3 * its.indices.size()));
}

std::vector<indexed_triangle_set> its_split(const indexed_triangle_set &its)
{
    return Slic3r::its_split<>(its);
}

// Number of disconnected patches (faces are connected if they share an edge, shared edge defined with 2 shared vertex indices).
size_t its_number_of_patches(const indexed_triangle_set &its)
{
    return Slic3r::its_number_of_patches<>(its);
}
size_t its_number_of_patches(const indexed_triangle_set &its, const std::vector<Index3> &face_neighbors)
{
    return its_number_of_patches<>(ItsNeighborsWrapper{ its, face_neighbors });
}

// Same as its_number_of_patches(its) > 1, but faster.
bool its_is_splittable(const indexed_triangle_set &its)
{
    return Slic3r::its_is_splittable<>(its);
}
bool its_is_splittable(const indexed_triangle_set &its, const std::vector<Index3> &face_neighbors)
{
    return its_is_splittable<>(ItsNeighborsWrapper{ its, face_neighbors });
}

size_t its_num_open_edges(const std::vector<Index3> &face_neighbors)
{
    size_t num_open_edges = 0;
    for (const Index3& neighbors : face_neighbors)
        for (int n : neighbors)
            if (n < 0)
                ++ num_open_edges;
    return num_open_edges;
}

std::vector<std::pair<int, int>> its_get_open_edges(const indexed_triangle_set& its)
{
    std::vector<std::pair<int, int>> ret;
    std::vector<Index3> face_neighbors = its_face_neighbors(its);
    for (size_t i = 0; i < face_neighbors.size(); ++i) {
        for (size_t j = 0; j < 3; ++j) {
            if (face_neighbors[i][j] < 0) {
                const Domain::Index2 edge_indices = its_triangle_edge(its.indices[i], j);
                ret.emplace_back(edge_indices[0], edge_indices[1]);
            }
        }
    }
    return ret;
}

size_t its_num_open_edges(const indexed_triangle_set &its)
{
    return its_num_open_edges(its_face_neighbors(its));
}

void VertexFaceIndex::create(const indexed_triangle_set &its)
{
    m_vertex_to_face_start.assign(its.vertices.size() + 1, 0);
    // 1) Calculate vertex incidence by scatter.
    for (auto &face : its.indices) {
        ++ m_vertex_to_face_start[face[0] + 1];
        ++ m_vertex_to_face_start[face[1] + 1];
        ++ m_vertex_to_face_start[face[2] + 1];
    }
    // 2) Prefix sum to calculate offsets to m_vertex_faces_all.
    for (size_t i = 2; i < m_vertex_to_face_start.size(); ++ i)
        m_vertex_to_face_start[i] += m_vertex_to_face_start[i - 1];
    // 3) Scatter indices of faces incident to a vertex into m_vertex_faces_all.
    m_vertex_faces_all.assign(m_vertex_to_face_start.back(), 0);
    for (size_t face_idx = 0; face_idx < its.indices.size(); ++ face_idx) {
        auto &face = its.indices[face_idx];
        for (int i = 0; i < 3; ++ i)
            m_vertex_faces_all[m_vertex_to_face_start[face[i]] ++] = face_idx;
    }
    // 4) The previous loop modified m_vertex_to_face_start. Revert the change.
    for (auto i = int(m_vertex_to_face_start.size()) - 1; i > 0; -- i)
        m_vertex_to_face_start[i] = m_vertex_to_face_start[i - 1];
    m_vertex_to_face_start.front() = 0;
}

std::vector<Index3> its_face_neighbors(const indexed_triangle_set &its)
{
    using Slic3r::Biz::Algorithms::Execution::ex_seq;
    return create_face_neighbors_index(ex_seq, its);
}

std::vector<Index3> its_face_neighbors_par(const indexed_triangle_set &its)
{
    using Slic3r::Biz::Algorithms::Execution::ex_tbb;
    return create_face_neighbors_index(ex_tbb, its);
}

std::vector<Vec3f> its_face_normals(const indexed_triangle_set &its) 
{
    std::vector<Vec3f> normals;
    normals.reserve(its.indices.size());
    for (stl_triangle_vertex_indices face : its.indices)
        normals.push_back(its_face_normal(its, face));
    return normals;
}




} // namespace Slic3r
