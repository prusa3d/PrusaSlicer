// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_FIND_INTERSECTIONS_H
#define IGL_FIND_INTERSECTIONS_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  // Forward declaration
  template <typename DerivedV, int DIM> class AABB;
  /// Find intersecting pairs of triangles between two triangle meshes (V1,F1)
  /// and (V2,F2) using an AABB tree over the first mesh and the fast
  /// (non-exact) Guigue–Devillers triangle-triangle test
  /// (igl::tri_tri_intersection_test_3d). With `first_only`, returns as soon as
  /// a single intersecting pair is found — useful as a cheap predicate.
  ///
  /// \note This is the inexact, floating-point counterpart of
  /// igl::predicates::find_intersections; prefer the latter when exactness
  /// matters. Unlike that routine this does not special-case self-intersection
  /// (shared edges/vertices between (V1,F1) and (V2,F2) will be reported), so it
  /// is intended for two _distinct_ meshes.
  ///
  /// @param[in] tree1  AABB tree over (V1,F1)
  /// @param[in] V1  #V1 by 3 list of first-mesh vertex positions
  /// @param[in] F1  #F1 by 3 list of first-mesh triangle indices into V1
  /// @param[in] V2  #V2 by 3 list of second-mesh vertex positions
  /// @param[in] F2  #F2 by 3 list of second-mesh triangle indices into V2
  /// @param[in] first_only  stop after the first intersecting pair is found
  /// @param[out] IF  #IF by 2 list of intersecting pairs (f1,f2) into F1,F2
  /// @param[out] CP  #IF list of flags: whether each pair is coplanar
  /// @return true if any intersecting pair was found
  ///
  /// \see predicates::find_intersections, tri_tri_intersect
  template <
    typename DerivedV1,
    typename DerivedF1,
    typename DerivedV2,
    typename DerivedF2,
    typename DerivedIF,
    typename DerivedCP>
  IGL_INLINE bool find_intersections(
    const igl::AABB<DerivedV1,3> & tree1,
    const Eigen::MatrixBase<DerivedV1> & V1,
    const Eigen::MatrixBase<DerivedF1> & F1,
    const Eigen::MatrixBase<DerivedV2> & V2,
    const Eigen::MatrixBase<DerivedF2> & F2,
    const bool first_only,
    Eigen::PlainObjectBase<DerivedIF> & IF,
    Eigen::PlainObjectBase<DerivedCP> & CP);
  /// \overload
  /// \brief Builds the AABB tree over (V1,F1) internally.
  template <
    typename DerivedV1,
    typename DerivedF1,
    typename DerivedV2,
    typename DerivedF2,
    typename DerivedIF,
    typename DerivedCP>
  IGL_INLINE bool find_intersections(
    const Eigen::MatrixBase<DerivedV1> & V1,
    const Eigen::MatrixBase<DerivedF1> & F1,
    const Eigen::MatrixBase<DerivedV2> & V2,
    const Eigen::MatrixBase<DerivedF2> & F2,
    const bool first_only,
    Eigen::PlainObjectBase<DerivedIF> & IF,
    Eigen::PlainObjectBase<DerivedCP> & CP);
}
#ifndef IGL_STATIC_LIBRARY
#  include "find_intersections.cpp"
#endif
#endif
