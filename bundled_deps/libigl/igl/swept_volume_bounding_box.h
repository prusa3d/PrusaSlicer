// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SWEPT_VOLUME_BOUNDING_BOX_H
#define IGL_SWEPT_VOLUME_BOUNDING_BOX_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>
namespace igl
{
  /// Construct an axis-aligned bounding box containing a shape undergoing a
  /// motion sampled at a list of discrete rigid transformations.
  ///
  /// @param[in] V  #V by 3 list of mesh positions in reference pose
  /// @param[in] transforms  #transforms list of rigid transformations, one per
  ///     time step
  /// @param[out] box  box containing mesh under motion
  template <
    typename DerivedV,
    typename TScalar,
    typename TAlloc>
  IGL_INLINE void swept_volume_bounding_box(
    const Eigen::MatrixBase<DerivedV> & V,
    const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
      transforms,
    Eigen::AlignedBox<TScalar,3> & box);
}

#ifndef IGL_STATIC_LIBRARY
#  include "swept_volume_bounding_box.cpp"
#endif

#endif 
