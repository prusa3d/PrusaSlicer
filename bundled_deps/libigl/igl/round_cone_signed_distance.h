// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_ROUND_CONE_SIGNED_DISTANCE_H
#define IGL_ROUND_CONE_SIGNED_DISTANCE_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// Compute signed distance to a round cone (sphere-mesh capsule). 
  ///
  /// @param[in] p  3-vector query point
  /// @param[in] a  3-vector position of the first vertex
  /// @param[in] b  3-vector position of the second vertex
  /// @param[in] r1  radius at vertex a
  /// @param[in] r2  radius at vertex b
  /// @return signed distance to the round cone
  template <
    typename Derivedp,
    typename Deriveda, 
    typename Derivedb>
  IGL_INLINE typename Derivedp::Scalar round_cone_signed_distance(
    const Eigen::MatrixBase<Derivedp> & p, 
    const Eigen::MatrixBase<Deriveda> & a, 
    const Eigen::MatrixBase<Derivedb> & b, 
    const typename Derivedp::Scalar & r1, 
    const typename Derivedp::Scalar & r2);

  /// Compute signed distance to a round cone (sphere-mesh capsule) with
  /// pre-computed values that are independent of the query point.
  ///
  /// @param[in] p  3-vector query point
  /// @param[in] a  3-vector position of the first vertex
  /// @param[in] r1  radius at vertex a
  /// @param[in] r2  radius at vertex b
  /// @param[in] ba  3-vector direction from a to b
  /// @param[in] l2  squared length of ba
  /// @param[in] rr  r1 - r2
  /// @param[in] a2  l2 - rr*rr
  /// @param[in] il2  1/l2
  /// @return signed distance to the round cone
  template <
    typename Derivedp,
    typename Deriveda, 
    typename Derivedba>
  IGL_INLINE typename Derivedp::Scalar round_cone_signed_distance(
    const Eigen::MatrixBase<Derivedp> & p, 
    const Eigen::MatrixBase<Deriveda> & a, 
    const typename Derivedp::Scalar & r1, 
    const typename Derivedp::Scalar & r2,
    const Eigen::MatrixBase<Derivedba> & ba,
    const typename Derivedp::Scalar & l2,
    const typename Derivedp::Scalar & rr,
    const typename Derivedp::Scalar & a2,
    const typename Derivedp::Scalar & il2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "round_cone_signed_distance.cpp"
#endif
#endif 
