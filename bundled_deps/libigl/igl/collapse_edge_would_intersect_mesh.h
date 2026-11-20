// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COLLAPSE_EDGE_WOULD_INTERSECT_MESH_H
#define IGL_COLLAPSE_EDGE_WOULD_INTERSECT_MESH_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  // Forward declaration
  template <typename DerivedV, int DIM> class AABB;
  /// Determine if collapsing the edge `e` (placing the merged vertex at `p`)
  /// would cause the one-ring of the collapse to intersect a _separate, static_
  /// mesh `(VB,FB)` accelerated by `tree`.
  ///
  /// Unlike igl::collapse_edge_would_create_intersections (which looks for
  /// _self_-intersections of the mesh being decimated using a dynamic tree that
  /// tracks that mesh), this routine tests the reshaped one-ring against a fixed
  /// obstacle mesh whose AABB tree never changes during decimation.
  ///
  /// Only _transversal_ (non-coplanar) triangle-triangle intersections are
  /// reported. This is deliberate: if `(VB,FB)` happens to coincide with (part
  /// of) the mesh being decimated — for example when the static mesh _is_ the
  /// original input surface — then triangles that merely lie flush against the
  /// obstacle (sliding tangentially along it) are not reported, while triangles
  /// that actually pass through the obstacle are.
  ///
  /// \note This routine is exact for a _disjoint_ obstacle (the intended use,
  /// e.g. an offset/clearance shape). When the obstacle shares vertices with the
  /// mesh being decimated, obstacle triangles that share a fixed corner of a
  /// reshaped one-ring triangle are heuristically ignored (to permit tangential
  /// contact); consequently a genuine pierce that occurs very near such a shared
  /// vertex may be missed. Prefer a disjoint obstacle, or
  /// igl::collapse_edge_would_create_intersections for true self-intersections.
  ///
  /// @param[in] e  index into E of edge to try to collapse. E(e,:) = [s d] or
  ///     [d s], then d is collapsed to s.
  /// @param[in] p  dim list of vertex position where the merged vertex is placed
  /// @param[in] V  #V by dim list of vertex positions of the mesh being decimated
  /// @param[in] F  #F by 3 list of face indices into V
  /// @param[in] E  #E by 2 list of edge indices into V
  /// @param[in] EMAP #F*3 list of indices into E, mapping each directed edge to
  ///     its unique edge in E
  /// @param[in] EF  #E by 2 list of edge flaps
  /// @param[in] EI  #E by 2 list of edge flap corners
  /// @param[in] VB  #VB by dim list of vertex positions of the static mesh
  /// @param[in] FB  #FB by 3 list of face indices into VB
  /// @param[in] tree AABB tree whose leaves correspond to the faces of (VB,FB)
  /// @param[in] inf_face_id  faces of (V,F) with index `>= inf_face_id` are
  ///     ignored (e.g., fictitious faces incident on a point at infinity). Pass
  ///     a negative value to consider all faces.
  /// @return true if the collapse would create a (transversal) intersection with
  ///     the static mesh
  ///
  /// \see collapse_edge_would_create_intersections, block_intersections_with_input
  template <
    typename Derivedp,
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedEMAP,
    typename DerivedEF,
    typename DerivedEI,
    typename DerivedVB,
    typename DerivedFB>
  IGL_INLINE bool collapse_edge_would_intersect_mesh(
    const int e,
    const Eigen::MatrixBase<Derivedp> & p,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<DerivedEMAP> & EMAP,
    const Eigen::MatrixBase<DerivedEF> & EF,
    const Eigen::MatrixBase<DerivedEI> & EI,
    const Eigen::MatrixBase<DerivedVB> & VB,
    const Eigen::MatrixBase<DerivedFB> & FB,
    const igl::AABB<DerivedVB,3> & tree,
    const int inf_face_id = -1);
}
#ifndef IGL_STATIC_LIBRARY
#  include "collapse_edge_would_intersect_mesh.cpp"
#endif
#endif
