// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2026 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_TRIANGLE_REMESH_AT_POINTS_H
#define IGL_TRIANGLE_REMESH_AT_POINTS_H
#include "../igl_inline.h"

#include <Eigen/Dense>
#include <Eigen/Sparse>

namespace igl 
{
namespace triangle
{
  ///   Given a set of unique points on a mesh specified by barycentric
  ///   coordinates and a triangle index, remesh the mesh to include
  ///   these points as vertices. Barycentric coordinates should be non-negative
  ///   and sum to 1. Exact floating point equality with 0.0 is used to
  ///   determine if a point is on a vertex or edge (user can round to 0.0 if
  ///   they want to treat points as being on a vertex or edge; otherwise we are
  ///   at the mercy of `triangle`). Vertex-points are not inserted (but tracked
  ///   in `K`). Edge-points are inserted and the edge is split on all incident
  ///   faces (the input can be non-manifold, so this might be more than 2).
  ///   Face-points are inserted. The output preserves the (non-)manifoldness of the
  ///   input.
  ///
  ///   The input mesh can be in 3D (or any dimension).
  ///
  ///   @param[in] V  #V by dim list of mesh vertex positions
  ///   @param[in] F  #F by 3 list of triangle indices into rows of V
  ///   @param[in] B  #B by 3 list of barycentric coordinates, ith row are
  ///   coordinates of ith sampled point in face FI(i)
  ///   @param[in] FI  #B list of indices into F
  ///   @param[out] VV  #VV by dim list of mesh vertex positions (top #V rows is
  ///   always V; bottom #B rows are new vertices)
  ///   @param[out] FF  #FF by 3 list of triangle indices into rows of VV
  ///   @param[out] J  #FF list of indices into F 
  ///   @param[out] K  #B list of indices into VV
  ///
  ///   \note Each original triangles new triangulation will
  ///   be Delaunay in `barycentric coordinate space` without additional Steiner
  ///   points. This could be fixed with an affine map (yet to be implemented).
  template <
    typename DerivedV, 
    typename DerivedF, 
    typename DerivedB,
    typename DerivedFI,
    typename DerivedVV,
    typename DerivedFF,
    typename DerivedJ,
    typename DerivedK
    >
  IGL_INLINE void remesh_at_points(
    const Eigen::MatrixBase<DerivedV> & V, 
    const Eigen::MatrixBase<DerivedF> & F, 
    const Eigen::MatrixBase<DerivedB> & B,
    const Eigen::MatrixBase<DerivedFI> & FI,
    Eigen::PlainObjectBase<DerivedVV> & VV,
    Eigen::PlainObjectBase<DerivedFF> & FF,
    Eigen::PlainObjectBase<DerivedJ> & J,
    Eigen::PlainObjectBase<DerivedK> & K);
}
}

#ifndef IGL_STATIC_LIBRARY
#  include "remesh_at_points.cpp"
#endif

#endif

