#ifndef libslic3r_MeshBoolean_hpp_
#define libslic3r_MeshBoolean_hpp_

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include <memory>
#include <exception>
#include <Eigen/Geometry>
#include <utility>
#include <vector>

#include "admesh/stl.h"

namespace Slic3r::Biz::CGAL::Algorithms::MeshBoolean {

using EigenMesh = std::pair<Eigen::MatrixXd, Eigen::MatrixXi>;

Domain::TriangleMesh eigen_to_triangle_mesh(const EigenMesh &emesh);
EigenMesh triangle_mesh_to_eigen(const Domain::TriangleMesh &mesh);

void minus(EigenMesh &A, const EigenMesh &B);
void self_union(EigenMesh &A);
    
void minus(Domain::TriangleMesh& A, const Domain::TriangleMesh& B);
void self_union(Domain::TriangleMesh& mesh);

namespace cgal {

struct CGALMesh;

struct CGALMeshDeleter { void operator()(CGALMesh *ptr); };
using CGALMeshPtr = std::unique_ptr<CGALMesh, CGALMeshDeleter>;

CGALMeshPtr clone(const CGALMesh &m);

CGALMeshPtr triangle_mesh_to_cgal(
    const std::vector<stl_vertex> &V,
    const std::vector<stl_triangle_vertex_indices> &F);

inline CGALMeshPtr triangle_mesh_to_cgal(const indexed_triangle_set &M)
{
    return triangle_mesh_to_cgal(M.vertices, M.indices);
}
inline CGALMeshPtr triangle_mesh_to_cgal(const Domain::TriangleMesh &M)
{
    return triangle_mesh_to_cgal(M.its);
}

Domain::TriangleMesh cgal_to_triangle_mesh(const CGALMesh &cgalmesh);
indexed_triangle_set cgal_to_indexed_triangle_set(const CGALMesh &cgalmesh);

// Do boolean mesh difference with CGAL bypassing igl.
void minus(Domain::TriangleMesh &A, const Domain::TriangleMesh &B);
void plus(Domain::TriangleMesh &A, const Domain::TriangleMesh &B);
void intersect(Domain::TriangleMesh &A, const Domain::TriangleMesh &B);

void minus(indexed_triangle_set &A, const indexed_triangle_set &B);
void plus(indexed_triangle_set &A, const indexed_triangle_set &B);
void intersect(indexed_triangle_set &A, const indexed_triangle_set &B);

void minus(CGALMesh &A, CGALMesh &B);
void plus(CGALMesh &A, CGALMesh &B);
void intersect(CGALMesh &A, CGALMesh &B);

bool does_self_intersect(const Domain::TriangleMesh &mesh);
bool does_self_intersect(const CGALMesh &mesh);

bool does_bound_a_volume(const CGALMesh &mesh);
bool empty(const CGALMesh &mesh);

} // namespace cgal

} // namespace Slic3r::MeshBoolean

#endif // libslic3r_MeshBoolean_hpp_
