// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BOX_SIMPLICES_H
#define IGL_BOX_SIMPLICES_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// 
  /// 
  /// @param[in] V  #V by dim list of vertex positions
  /// @param[in] F #F by ss list of simplex indices into V
  /// @param[out] B1  #B by dim list of minimum corners of the Eytzinger AABBs
  /// @param[out] B2  #B by dim list of maximum corners of the Eytzinger AABBs
  ///
  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedB
  >
  IGL_INLINE void box_simplices(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedB> & B1,
    Eigen::PlainObjectBase<DerivedB> & B2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "box_simplices.cpp"
#endif
#endif 
