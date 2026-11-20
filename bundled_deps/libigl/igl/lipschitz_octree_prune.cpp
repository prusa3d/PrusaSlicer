// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "lipschitz_octree_prune.h"
#include "parallel_for.h"
#include <cassert>
#include <cmath>

template <
  bool batched,
  typename Derivedorigin,
  typename Func,
  typename Derivedijk,
  typename Derivedijk_maybe
    >
IGL_INLINE void igl::lipschitz_octree_prune(
  const Eigen::MatrixBase<Derivedorigin> & origin,
  const typename Derivedorigin::Scalar h0,
  const int depth,
  const Func & udf,
  const Eigen::MatrixBase<Derivedijk> & ijk,
  Eigen::PlainObjectBase<Derivedijk_maybe> & ijk_maybe)
{
  static_assert(
    (Derivedorigin::RowsAtCompileTime == 1 && (
      Derivedorigin::ColsAtCompileTime == 3 ||
      Derivedorigin::ColsAtCompileTime == Eigen::Dynamic)) ||
    (Derivedorigin::ColsAtCompileTime == 1 && (
      Derivedorigin::RowsAtCompileTime == 3 ||
      Derivedorigin::RowsAtCompileTime == Eigen::Dynamic)),
    "Derivedorigin must be a vector with 3 or Eigen::Dynamic dimensions");
  assert((origin.rows() == 3 || origin.cols() == 3) && origin.size() == 3 &&
    "origin must be a 3D vector");

  using Scalar = typename Derivedorigin::Scalar;
  using RowVectorS3 = Eigen::Matrix<Scalar,1,3>;
  using MatrixSX3R = Eigen::Matrix<Scalar,Eigen::Dynamic,3,Eigen::RowMajor>;

  // Cell side length at this depth.
  const Scalar h = h0 / (1 << depth);
  // A cell's center is at most h*sqrt(3)/2 from any point inside it (half
  // space-diagonal).  By 1-Lipschitz, if udf(center) > h*sqrt(3)/2 the
  // entire cell has udf > 0, so it contains no zero-crossing.
  const Scalar threshold = h * std::sqrt(Scalar(3)) / 2;

  // Compute cell centers.  Coordinate convention: x=col1, y=col0, z=col2
  // (matches unique_sparse_voxel_corners / marching_cubes).
  const auto cell_center = [&](const int c) -> RowVectorS3
  {
    return RowVectorS3(
      origin(0) + h * (ijk(c,1) + Scalar(0.5)),
      origin(1) + h * (ijk(c,0) + Scalar(0.5)),
      origin(2) + h * (ijk(c,2) + Scalar(0.5)));
  };

  Eigen::Array<bool,Eigen::Dynamic,1> keep(ijk.rows());
  // Requires C++17
  if constexpr (batched)
  {
    MatrixSX3R centers(ijk.rows(), 3);
    for(int c = 0; c < ijk.rows(); c++) centers.row(c) = cell_center(c);
    const Eigen::Matrix<Scalar,Eigen::Dynamic,1> u = udf(centers);
    for(int c = 0; c < ijk.rows(); c++)
    {
      assert(u(c) >= 0 && "udf must be non-negative for lipschitz_octree_prune");
      keep(c) = (u(c) <= threshold);
    }
  }
  else
  {
    igl::parallel_for(ijk.rows(), [&](const int c)
    {
      const Scalar u = udf(cell_center(c));
      assert(u >= 0 && "udf must be non-negative for lipschitz_octree_prune");
      keep(c) = (u <= threshold);
    }, 1000);
  }

  ijk_maybe.resize(ijk.rows(), 3);
  int k = 0;
  for(int c = 0; c < ijk.rows(); c++)
    if(keep(c)) ijk_maybe.row(k++) = ijk.row(c);
  ijk_maybe.conservativeResize(k, 3);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::lipschitz_octree_prune<false,Eigen::Matrix<double, 1, 3, 1, 1, 3>, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar, int, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>>&);
template void igl::lipschitz_octree_prune<true, Eigen::Matrix<double, 1, 3, 1, 1, 3>, std::function<Eigen::Matrix<double, -1, 1, 0, -1, 1> (Eigen::Matrix<double, -1, 3, 1, -1, 3> const&)>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar, int, std::function<Eigen::Matrix<double, -1, 1, 0, -1, 1> (Eigen::Matrix<double, -1, 3, 1, -1, 3> const&)> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>>&);
#endif

