// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SWEPT_VOLUME_SIGNED_DISTANCE_H
#define IGL_SWEPT_VOLUME_SIGNED_DISTANCE_H
#include "igl_inline.h"
#include "signed_distance.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>
namespace igl
{
  /// Compute the signed distance to a sweep surface of a mesh under-going a
  /// rigid motion discretely sampled at a list of transformations at a grid.
  ///
  /// @param[in] V  #V by 3 list of mesh positions in reference pose
  /// @param[in] F  #F by 3 list of triangle indices [0,n)
  /// @param[in] transforms  #transforms list of rigid transformations, one per
  ///     time step
  /// @param[in] sign_type  method for computing distance _sign_
  /// @param[in] GV  #GV by 3 list of evaluation point grid positions
  /// @param[in] res  3-long resolution of GV grid
  /// @param[in] h  edge-length of grid
  /// @param[in] isolevel  isolevel to "focus" on; grid positions far enough away from
  ///     isolevel (based on h) will get approximate values). Set
  ///     isolevel=infinity to get good values everywhere (slow and
  ///     unnecessary if just trying to extract isolevel-level set).
  /// @param[in] S0  #GV initial values (will take minimum with these), can be same
  ///     as S)
  /// @param[out] S  #GV list of signed distances
  template <
    typename DerivedV,
    typename DerivedF,
    typename TScalar,
    typename TAlloc,
    typename DerivedGV,
    typename Derivedres,
    typename DerivedS0,
    typename DerivedS>
  IGL_INLINE void swept_volume_signed_distance(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
      transforms,
    const SignedDistanceType sign_type,
    const Eigen::MatrixBase<DerivedGV> & GV,
    const Eigen::MatrixBase<Derivedres> & res,
    const typename DerivedGV::Scalar h,
    const typename DerivedGV::Scalar isolevel,
    const Eigen::MatrixBase<DerivedS0> & S0,
    Eigen::PlainObjectBase<DerivedS> & S);
  /// \overload
  template <
    typename DerivedV,
    typename DerivedF,
    typename TScalar,
    typename TAlloc,
    typename DerivedGV,
    typename Derivedres,
    typename DerivedS>
  IGL_INLINE void swept_volume_signed_distance(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
      transforms,
    const SignedDistanceType sign_type,
    const Eigen::MatrixBase<DerivedGV> & GV,
    const Eigen::MatrixBase<Derivedres> & res,
    const typename DerivedGV::Scalar h,
    const typename DerivedGV::Scalar isolevel,
    Eigen::PlainObjectBase<DerivedS> & S);
  }

#ifndef IGL_STATIC_LIBRARY
#  include "swept_volume_signed_distance.cpp"
#endif 

#endif
