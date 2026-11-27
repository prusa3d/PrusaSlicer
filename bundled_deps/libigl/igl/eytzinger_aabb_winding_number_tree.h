// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2026 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EYTZINGER_AABB_WINDING_NUMBER_TREE_H
#define IGL_EYTZINGER_AABB_WINDING_NUMBER_TREE_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// For each internal node of the Eytzinger AABB tree, compute a streamed list
  /// of pairs of vertices which would "close" the winding number contributions 
  /// of all segments in the subtree rooted at that node [Jacobson et al. 2013]
  /// 
  /// @param[in] E  #E by 2 list of segment endpoint vertex indices
  /// @param[out] leaf #B list of leaf indices, -1 indicates internal node, -2
  /// indicates empty node
  /// @param[out] I  #I streamed list of vertex indices I=[(s₀,s₁), (s₂,s₃), ...]
  /// @param[out] C  #C list of cumulative counts into I for each internal node,
  /// so that the pairs for internal node i are found in I[C(i):C(i+1)-1]
  ///
  template <
    typename DerivedE,
    typename Derivedleaf,
    typename DerivedI,
    typename DerivedC>
  IGL_INLINE void eytzinger_aabb_winding_number_tree(
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<Derivedleaf> & leaf,
    Eigen::PlainObjectBase<DerivedI> & I,
    Eigen::PlainObjectBase<DerivedC> & C);
}

#ifndef IGL_STATIC_LIBRARY
#include "eytzinger_aabb_winding_number_tree.cpp"
#endif

#endif
