// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EYTZINGER_AABB_SDF_H
#define IGL_EYTZINGER_AABB_SDF_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// 
  /// @param[in] B1  #B by dim list of minimum corners of the Eytzinger AABBs
  /// @param[in] B2  #B by dim list of maximum corners of the Eytzinger AABBs
  /// @param[in] leaf #B list of leaf indices, -1 indicates internal node, -2
  /// indicates empty node
  /// @param[in] primitive  function handle that takes as input a primitive id
  /// and returns the SDF to that primitive `primitive(i)`
  /// @param[out] f  SDF value at query point x
  ///
  /// eytzinger_aabb_sdf<false> computes the signed distance function and
  /// assumes that primitive(i) returns the signed distance to primitive i
  ///
  /// eytzinger_aabb_sdf<true> computes the squared (unsigned) distance function
  /// and assumes that primitive(i) returns the squared distance to primitive i
  template <
    bool Squared,
    typename Derivedp,
    typename Func,
    typename DerivedB,
    typename Derivedleaf
  >
  IGL_INLINE void eytzinger_aabb_sdf(
    const Eigen::MatrixBase<Derivedp> & p,
    const Func & primitive,
    const Eigen::MatrixBase<DerivedB> & B1,
    const Eigen::MatrixBase<DerivedB> & B2,
    const Eigen::MatrixBase<Derivedleaf> & leaf,
    typename Derivedp::Scalar & f);

  /// @param[in] P  #P by dim list of query points
  /// @param[in] B1  #B by dim list of minimum corners of the Eytzinger AABBs
  /// @param[in] B2  #B by dim list of maximum corners of the Eytzinger AABBs
  /// @param[in] leaf #B list of leaf indices, -1 indicates internal node, -2
  /// indicates empty node
  /// @param[in] primitive  function handle that takes as input the query point
  ///   (row of P) and a primitive id and returns the SDF to that primitive
  ///   `primitive(p,i)`
  /// @param[out] S  #P list of SDF values at query points P
  template <
    bool Squared,
    typename DerivedP,
    typename Func,
    typename DerivedB,
    typename Derivedleaf,
    typename DerivedS
  >
  IGL_INLINE void eytzinger_aabb_sdf(
    const Eigen::MatrixBase<DerivedP> & P,
    const Func & primitive,
    const Eigen::MatrixBase<DerivedB> & B1,
    const Eigen::MatrixBase<DerivedB> & B2,
    const Eigen::MatrixBase<Derivedleaf> & leaf,
    Eigen::PlainObjectBase<DerivedS> & S);
}

#ifndef IGL_STATIC_LIBRARY
#  include "eytzinger_aabb_sdf.cpp"
#endif
#endif 
