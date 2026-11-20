// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "swept_volume_bounding_box.h"

template <
  typename DerivedV,
  typename TScalar,
  typename TAlloc>
IGL_INLINE void igl::swept_volume_bounding_box(
  const Eigen::MatrixBase<DerivedV> & V,
  const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
    transforms,
  Eigen::AlignedBox<TScalar,3> & box)
{
  box.setEmpty();
  // Find extent over all time steps
  for(const Eigen::Transform<TScalar,3,Eigen::Affine> & T : transforms)
  {
    for(int vi = 0;vi<V.rows();vi++)
    {
      const Eigen::Matrix<TScalar,3,1> v =
        V.row(vi).transpose().template cast<TScalar>();
      box.extend(T*v);
    }
  }
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::swept_volume_bounding_box<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  double,
  Eigen::aligned_allocator<Eigen::Transform<double,3,Eigen::Affine> > >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  std::vector<
    Eigen::Transform<double,3,Eigen::Affine>,
    Eigen::aligned_allocator<Eigen::Transform<double,3,Eigen::Affine> > > const &,
  Eigen::AlignedBox<double,3> &);
template void igl::swept_volume_bounding_box<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  double,
  std::allocator<Eigen::Transform<double,3,Eigen::Affine> > >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  std::vector<
    Eigen::Transform<double,3,Eigen::Affine>,
    std::allocator<Eigen::Transform<double,3,Eigen::Affine> > > const &,
  Eigen::AlignedBox<double,3> &);
#endif
