// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/

#ifndef IGL_ORIENTED_BOUNDING_BOX_H
#define IGL_ORIENTED_BOUNDING_BOX_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>
namespace igl
{
  enum OrientedBoundingBoxMinimizeType
  {
    ORIENTED_BOUNDING_BOX_MINIMIZE_VOLUME = 0,
    ORIENTED_BOUNDING_BOX_MINIMIZE_SURFACE_AREA = 1,
    ORIENTED_BOUNDING_BOX_MINIMIZE_DIAGONAL_LENGTH = 2,
    NUM_ORIENTED_BOUNDING_BOX_MINIMIZE_TYPES = 3,
  };
  /// Given a set of points compute the rotation transformation of them such
  /// that their axis-aligned bounding box is as small as possible.
  ///
  /// Consider passing the points on the convex hull of original list of points.
  ///
  /// @param[in] P  #P by 3 list of point locations
  /// @param[in] n  number of rotations to try
  /// @param[in] minimize_type  which quantity to minimize
  /// @param[out] R  rotation matrix
  template <typename DerivedP, typename DerivedR>
  IGL_INLINE void oriented_bounding_box(
    const Eigen::MatrixBase<DerivedP>& P,
    const int n,
    const OrientedBoundingBoxMinimizeType minimize_type,
    Eigen::PlainObjectBase<DerivedR> & R);
}

#ifndef IGL_STATIC_LIBRARY
#include "oriented_bounding_box.cpp"
#endif
#endif
