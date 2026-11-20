// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LIPSCHITZ_OCTREE_H
#define IGL_LIPSCHITZ_OCTREE_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl 
{
  /// Given a minimum corner position (origin) and a side length (h0) and a
  /// maximum depth (max_depth), determine the possible active leaf octree cells
  /// based on an one-Lipschitz non-negative function to a level set (e.g.,
  /// "unsigned distance function").
  ///
  ///   @param[in] origin  3-vector of root cell origin (minimum corner)
  ///   @param[in] h0   side length of root cell
  ///   @param[in] max_depth  maximum depth of octree (root is depth=0)
  ///   @param[in] udf  1-Lipschitz function of (unsigned) distance to level set
  ///   @param[out] ijk #ijk by 3 list of octree leaf cell minimum corner
  ///     subscripts
  template <
    bool batched=false,
    typename Derivedorigin,
    typename Func,
    typename Derivedijk
      >
  IGL_INLINE void lipschitz_octree(
    const Eigen::MatrixBase<Derivedorigin> & origin,
    const typename Derivedorigin::Scalar h0,
    const int max_depth,
    const Func & udf,
    Eigen::PlainObjectBase<Derivedijk> & ijk);
}

#ifndef IGL_STATIC_LIBRARY
#    include "lipschitz_octree.cpp"
#endif

#endif

