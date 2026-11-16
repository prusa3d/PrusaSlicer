// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "unique_sparse_voxel_corners.h"
#include "matlab_format.h"
#include "unique.h"
#include <cassert>
#include <iostream>

namespace
{
  // Match the yxz binary counting order in marching_cubes/sparse_voxel_grid
  const int marching_cubes_reoder[] = {1,0,2,3,5,4,6,7};
}

template <
  typename Derivedijk,
  typename Derivedunique_ijk,
  typename DerivedJ
    >
IGL_INLINE void igl::unique_sparse_voxel_corners(
  const int depth,
  const Eigen::MatrixBase<Derivedijk> & ijk,
  Eigen::PlainObjectBase<Derivedunique_ijk> & unique_ijk,
  Eigen::PlainObjectBase<DerivedJ> & J)
{
  using MatrixiX3R = Eigen::Matrix<int,Eigen::Dynamic,3,Eigen::RowMajor>;
  // slow, precise hashing. Not sure why I didn't just use unique_rows on #ijk*8
  // by 3...
  const Eigen::Matrix<std::int64_t,1,3> coeffs(
    1,
    //static_cast<std::int64_t>(std::pow(2,depth) + 1), 
    (1 << depth) + 1,
    //static_cast<std::int64_t>(std::pow(2,depth) + 1) * (std::pow(2,depth) + 1));
    ((1 << depth) +1)*((1 << depth) +1));
  const auto ijk2code = [&coeffs](const int i, const int j, const int k)->std::int64_t
  {
    // code = i*(2.^depth + 1)^0 + j*(2.^depth + 1)^1 + k*(2.^depth + 1)^2;
    // Probably can just use 2.^(depth+1) instead of (2.^depth + 1) and use bit
    // shifting?
    const std::int64_t code =
      static_cast<std::int64_t>(i) * coeffs[0] +
      static_cast<std::int64_t>(j) * coeffs[1] +
      static_cast<std::int64_t>(k) * coeffs[2];
    return code;
  };
  const auto code2ijk = [&coeffs](const std::int64_t code, int & i, int & j, int & k)
  {
    k = static_cast<int>(code / coeffs[2]);
    j = static_cast<int>((code - k * coeffs[2]) / coeffs[1]);
    i = static_cast<int>(code - j * coeffs[1] - k * coeffs[2]);
  };

  // Should use parallel_for
  Eigen::Matrix<std::int64_t,Eigen::Dynamic,8,Eigen::RowMajor> codes(ijk.rows(),8);
  for(int c = 0;c<ijk.rows();c++)
  {
    for(int i = 0;i<8;i++)
    {
      Eigen::RowVector3i ijk_c(
        ijk(c,0) + ((i&2) ? 1 : 0), 
        ijk(c,1) + ((i&4) ? 1 : 0), 
        ijk(c,2) + ((i&1) ? 1 : 0));
      const std::int64_t code = ijk2code(ijk_c(0), ijk_c(1), ijk_c(2));
      const int k = marching_cubes_reoder[i];
      codes(c,k) = code;
#ifndef NDEBUG
      Eigen::RowVector3i ijk_c_check;
      code2ijk(code,ijk_c_check(0),ijk_c_check(1),ijk_c_check(2));
      //printf("ijk2code(%d,%d,%d) = %ld\n",
      //       ijk_c(0),ijk_c(1),ijk_c(2),code);
      //printf("  code2ijk(%ld) = %d,%d,%d\n",
      //    code, ijk_c_check(0),ijk_c_check(1),ijk_c_check(2));
      // Check that the code is correct
      assert(ijk_c_check(0) == ijk_c(0) && ijk_c_check(1) == ijk_c(1) &&
             ijk_c_check(2) == ijk_c(2) &&
             "ijk2code and code2ijk are not consistent");
#endif
    }
  }
  // igl::unique is the bottleneck by far.
  // unique_rows might actually be faster because it doesn't do roundtrips to
  // std::vector
  Eigen::VectorXi I;
  {
    Eigen::Matrix<std::int64_t,Eigen::Dynamic,1> _;
    Eigen::Matrix<typename DerivedJ::Scalar,Eigen::Dynamic,1> Jvec;
    // igl::unique has a lot of internal copies :-(
    igl::unique(codes,_,I,Jvec);
    // reshape Jvec into ijk.rows() by 8
    J = Jvec.reshaped(ijk.rows(),8);
  }

  // Map of codes as vector
  const auto codes_vec = codes.reshaped();

  unique_ijk.resize(I.size(),3);
  for(int c = 0;c<I.size();c++)
  {
    Eigen::RowVector3i ijk_c;
    code2ijk(codes_vec(I(c)),ijk_c(0),ijk_c(1),ijk_c(2));
    unique_ijk.row(c) = ijk_c.cast<typename Derivedunique_ijk::Scalar>();
  }
}

template<
  typename Derivedorigin,
  typename Derivedijk,
  typename Derivedunique_ijk,
  typename DerivedJ,
  typename Derivedunique_corners
    >
IGL_INLINE void igl::unique_sparse_voxel_corners(
  const Eigen::MatrixBase<Derivedorigin> & origin,
  const typename Derivedorigin::Scalar h0,
  const int depth,
  const Eigen::MatrixBase<Derivedijk> & ijk,
  Eigen::PlainObjectBase<Derivedunique_ijk> & unique_ijk,
  Eigen::PlainObjectBase<DerivedJ> & J,
  Eigen::PlainObjectBase<Derivedunique_corners> & unique_corners)
{
  unique_sparse_voxel_corners(depth,ijk,unique_ijk,J);

  using Scalar = typename Derivedunique_corners::Scalar;
  const Scalar h = h0 / (1 << depth);
  unique_corners.resize(unique_ijk.rows(),3);
  for(int c = 0;c<unique_ijk.rows();c++)
  {
    unique_corners(c,0) = origin(0) + h * unique_ijk(c,1);
    unique_corners(c,1) = origin(1) + h * unique_ijk(c,0);
    unique_corners(c,2) = origin(2) + h * unique_ijk(c,2);
  }
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::unique_sparse_voxel_corners<Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 8, 1, -1, 8>>(int, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>>&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 8, 1, -1, 8>>&);
template void igl::unique_sparse_voxel_corners<Eigen::Matrix<double, 1, 3, 1, 1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 8, 1, -1, 8>, Eigen::Matrix<double, -1, 3, 1, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar, int, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>>&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 8, 1, -1, 8>>&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 1, -1, 3>>&);
#endif

