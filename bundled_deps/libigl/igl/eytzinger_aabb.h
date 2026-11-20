// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_EYTZINGER_AABB_H
#define IGL_EYTZINGER_AABB_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// Compute the Eytzinger AABB for a given mesh
  /// 
  /// @param[in] PB1  #P by dim list of minimum corners of the AABBs
  /// @param[in] PB2  #P by dim list of maximum corners of the AABBs
  /// @param[out] B1  #B by dim list of minimum corners of the Eytzinger AABBs
  /// @param[out] B2  #B by dim list of maximum corners of the Eytzinger AABBs
  /// @param[out] leaf #B list of leaf indices, -1 indicates internal node, -2
  /// indicates empty node
  ///
  template <
    typename DerivedPB,
    typename DerivedB,
    typename Derivedleaf
  >
  IGL_INLINE void eytzinger_aabb(
    const Eigen::MatrixBase<DerivedPB> & PB1,
    const Eigen::MatrixBase<DerivedPB> & PB2,
    Eigen::PlainObjectBase<DerivedB> & B1,
    Eigen::PlainObjectBase<DerivedB> & B2,
    Eigen::PlainObjectBase<Derivedleaf> & leaf);
}

#ifndef IGL_STATIC_LIBRARY
#  include "eytzinger_aabb.cpp"
#endif
#endif 
