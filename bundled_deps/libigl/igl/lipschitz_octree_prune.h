// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LIPSCHITZ_OCTREE_PRUNE_H
#define IGL_LIPSCHITZ_OCTREE_PRUNE_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl 
{
  /// Given a 1-Lipschitz non-negative function to a level set (e.g., "unsigned
  /// distance function") and a set of query octree cells at a current depth
  /// given by the minimum corners subscripts in ijk (where root is (0,0,0)),
  /// determine which of these cells maybe still contains the level set based on
  /// checking a simple sufficient culling condition at the corners.
  ///
  ///   @param[in] origin  3-vector of root cell origin (minimum corner)
  ///   @param[in] h0   side length of root cell
  ///   @param[in] depth  current depth (single root cell is depth=0)
  ///   @param[in] udf  1-Lipschitz function of (unsigned) distance to level set
  ///   @param[in] ijk #ijk by 3 list of octree leaf cell minimum corner
  ///   subscripts
  ///   @param[out] ijk_maybe #ijk_maybe by 3 list of octree leaf cell indices
  template <
    bool batched=false,
    typename Derivedorigin,
    typename Func,
    typename Derivedijk,
    typename Derivedijk_maybe
      >
  IGL_INLINE void lipschitz_octree_prune(
    const Eigen::MatrixBase<Derivedorigin> & origin,
    const typename Derivedorigin::Scalar h0,
    const int depth,
    const Func & udf,
    const Eigen::MatrixBase<Derivedijk> & ijk,
    Eigen::PlainObjectBase<Derivedijk_maybe> & ijk_maybe);
}

#ifndef IGL_STATIC_LIBRARY
#    include "lipschitz_octree_prune.cpp"
#endif

#endif



