// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2026 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SIMPLEX_SIMPLEX_SQUARED_DISTANCE_H
#define IGL_SIMPLEX_SIMPLEX_SQUARED_DISTANCE_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  /// Find the squared distance between closest points on simplices with corners
  /// V1 and V2, respectively. V1 and V2 don't have to be the same simplex size,
  /// but they must have the same number of columns (dimension). This function
  /// works recursively.
  ///
  /// @param[in] V1  #V1 by dim list of simplex corners
  /// @param[in] V2  #V2 by dim list of simplex corners
  /// @param[out] sqrdDist  squared distance between closest points on simplices
  /// @param[out] B1  #V1 list of barycentric coordinates of closest point on
  /// simplex 1
  /// @param[out] B2  #V2 list of barycentric coordinates of closest point on
  /// simplex 2
  template
    <typename DerivedV1,
     typename DerivedV2,
     typename sqrdType,
     typename DerivedB1,
     typename DerivedB2>
  IGL_INLINE void simplex_simplex_squared_distance(
    const Eigen::MatrixBase<DerivedV1> & V1,
    const Eigen::MatrixBase<DerivedV2> & V2,
    sqrdType & sqrdDist,
    Eigen::MatrixBase<DerivedB1> & B1,
    Eigen::MatrixBase<DerivedB2> & B2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "simplex_simplex_squared_distance.cpp"
#endif
#endif

