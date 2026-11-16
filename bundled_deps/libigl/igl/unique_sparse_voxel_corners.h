// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_UNIQUE_SPARSE_VOXEL_CORNERS_H
#define IGL_UNIQUE_SPARSE_VOXEL_CORNERS_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl 
{
  ///  Give a list of octree cells subscripts (ijk) (minimum corners) at a given depth,
  ///  determine a unique list of subscripts to all incident corners of those
  ///  cells (de-replicating shared corners).
  ///
  ///   @param[in] depth  current depth (single root cell is depth = 0)
  ///   @param[in] ijk #ijk by 3 list of octree leaf cell minimum corner
  ///   subscripts
  ///   @param[out] unique_ijk #unique_ijk by 3 list of unique corner subscripts
  ///   @param[out] J  #ijk by 8 list of indices into unique_ijk in yxz binary
  ///     counting order
  template<
    typename Derivedijk,
    typename Derivedunique_ijk,
    typename DerivedJ
      >
  IGL_INLINE void unique_sparse_voxel_corners(
    const int depth,
    const Eigen::MatrixBase<Derivedijk> & ijk,
    Eigen::PlainObjectBase<Derivedunique_ijk> & unique_ijk,
    Eigen::PlainObjectBase<DerivedJ> & J);

  /// \brief overload
  ///
  ///   @param[in] origin  3-vector of root cell minimum
  ///   @param[in] h0   side length of current depth level
  ///   @param[in] depth  current depth (single root cell is depth = 0)
  ///   @param[out] unique_corners #unique_ijk by 3 list of unique corner
  ///     positions
  template<
    typename Derivedorigin,
    typename Derivedijk,
    typename Derivedunique_ijk,
    typename DerivedJ,
    typename Derivedunique_corners
      >
  IGL_INLINE void unique_sparse_voxel_corners(
    const Eigen::MatrixBase<Derivedorigin> & origin,
    const typename Derivedorigin::Scalar h0,
    const int depth,
    const Eigen::MatrixBase<Derivedijk> & ijk,
    Eigen::PlainObjectBase<Derivedunique_ijk> & unique_ijk,
    Eigen::PlainObjectBase<DerivedJ> & J,
    Eigen::PlainObjectBase<Derivedunique_corners> & unique_corners);
}

#ifndef IGL_STATIC_LIBRARY
#    include "unique_sparse_voxel_corners.cpp"
#endif
#endif
