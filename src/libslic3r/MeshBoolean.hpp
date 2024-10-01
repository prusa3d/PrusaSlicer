///|/ Copyright (c) Prusa Research 2019 - 2023 Tomáš Mészáros @tamasmeszaros, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef libslic3r_MeshBoolean_hpp_
#define libslic3r_MeshBoolean_hpp_

#include <libslic3r/TriangleMesh.hpp>
#include <memory>
#include <exception>
#include <Eigen/Geometry>
#include <utility>
#include <vector>

#include "admesh/stl.h"
#include "libslic3r/Point.hpp"

namespace Slic3r {

namespace MeshBoolean {

using EigenMesh = std::pair<Eigen::MatrixXd, Eigen::MatrixXi>;

TriangleMesh eigen_to_triangle_mesh(const EigenMesh &emesh);
EigenMesh triangle_mesh_to_eigen(const TriangleMesh &mesh);

void minus(EigenMesh &A, const EigenMesh &B);
void self_union(EigenMesh &A);
    
void minus(TriangleMesh& A, const TriangleMesh& B);
void self_union(TriangleMesh& mesh);

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
inline CGALMeshPtr triangle_mesh_to_cgal(const TriangleMesh &M)
{
    return triangle_mesh_to_cgal(M.its);
}

TriangleMesh cgal_to_triangle_mesh(const CGALMesh &cgalmesh);
indexed_triangle_set cgal_to_indexed_triangle_set(const CGALMesh &cgalmesh);

// Do boolean mesh difference with CGAL bypassing igl.
void minus(TriangleMesh &A, const TriangleMesh &B);
void plus(TriangleMesh &A, const TriangleMesh &B);
void intersect(TriangleMesh &A, const TriangleMesh &B);

void minus(indexed_triangle_set &A, const indexed_triangle_set &B);
void plus(indexed_triangle_set &A, const indexed_triangle_set &B);
void intersect(indexed_triangle_set &A, const indexed_triangle_set &B);

void minus(CGALMesh &A, CGALMesh &B);
void plus(CGALMesh &A, CGALMesh &B);
void intersect(CGALMesh &A, CGALMesh &B);

bool does_self_intersect(const TriangleMesh &mesh);
bool does_self_intersect(const CGALMesh &mesh);

bool does_bound_a_volume(const CGALMesh &mesh);
bool empty(const CGALMesh &mesh);

}

} // namespace MeshBoolean
} // namespace Slic3r
#endif // libslic3r_MeshBoolean_hpp_
