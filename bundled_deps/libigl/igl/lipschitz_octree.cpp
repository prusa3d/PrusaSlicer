// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "lipschitz_octree.h"
#include "lipschitz_octree_prune.h"
#include "find.h"
#include "matlab_format.h"
#include <cassert>
#include <iostream>

template <
  bool batched,
  typename Derivedorigin,
  typename Func,
  typename Derivedijk
    >
IGL_INLINE void igl::lipschitz_octree(
  const Eigen::MatrixBase<Derivedorigin> & origin,
  const typename Derivedorigin::Scalar h0,
  const int max_depth,
  const Func & udf,
  Eigen::PlainObjectBase<Derivedijk> & ijk_out)
{
  using Scalar = typename Derivedorigin::Scalar;
  using RowVectorS3 = Eigen::Matrix<Scalar,1,3>;
  using MatrixSX8R = Eigen::Matrix<Scalar,Eigen::Dynamic,8,Eigen::RowMajor>;
  using MatrixiX3R = Eigen::Matrix<int,Eigen::Dynamic,3,Eigen::RowMajor>;

  // static assert to ensure that Derivedorigin is a vector and the
  // non-singleton dimension is 3 or Eigen::Dynamic
  static_assert(
    (Derivedorigin::RowsAtCompileTime == 1 && (
      Derivedorigin::ColsAtCompileTime == 3 ||
      Derivedorigin::ColsAtCompileTime == Eigen::Dynamic)) ||
    (Derivedorigin::ColsAtCompileTime == 1 && (
      Derivedorigin::RowsAtCompileTime == 3 ||
      Derivedorigin::RowsAtCompileTime == Eigen::Dynamic)),
    "Derivedorigin must be a vector with 3 or Eigen::Dynamic dimensions");
  // dynamic assert that the origin is a 3D vector
  assert((origin.rows() == 3 || origin.cols() == 3) && origin.size() == 3 &&
    "origin must be a 3D vector");

  MatrixiX3R ijk(1,3);
  ijk<<0,0,0;

  for(int depth = 0;depth<=max_depth;depth++)
  {
    if(ijk.rows() == 0)
    {
      // no more cells to refine
      break;
    }
    const Scalar h = h0 / (1 << depth);
    MatrixiX3R ijk_next;
    MatrixiX3R ijk_maybe;
    igl::lipschitz_octree_prune<batched>(origin,h0,depth,udf,ijk,ijk_maybe);
    if(depth == max_depth)
    {
      // sad copy
      ijk_out = ijk_maybe.template cast<typename Derivedijk::Scalar>();
      return;
    }else
    {
      const MatrixiX3R ijk_maybe_2 = ijk_maybe * 2;

      ijk_next.resize(ijk_maybe_2.rows()*8,3);
      for(int c = 0;c<ijk_maybe_2.rows();c++)
      {
        for(int i = 0;i<8;i++)
        {
          const int k = i;
          ijk_next.row(c*8+k) =
            ijk_maybe_2.row(c) +
            // This order shouldn't really matter, though it seems nice if it
            // matches above.
            Eigen::RowVector3i((i&2) ? 1 : 0, (i&4) ? 1 : 0, (i&1) ? 1 : 0);
        }
      }
    }
    if(depth == max_depth)
    {
      break;
    }
    ijk = ijk_next;
  }
  assert(false && "Should never reach here");
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::lipschitz_octree<false,Eigen::Matrix<double, 1, 3, 1, 1, 3>, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)>, Eigen::Matrix<int, -1, 3, 1, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar, int, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>>&);
template void igl::lipschitz_octree<true, Eigen::Matrix<double, 1, 3, 1, 1, 3>, std::function<Eigen::Matrix<double, -1, 1, 0, -1, 1> (Eigen::Matrix<double, -1, 3, 1, -1, 3> const&)>, Eigen::Matrix<int, -1, 3, 1, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar, int, std::function<Eigen::Matrix<double, -1, 1, 0, -1, 1> (Eigen::Matrix<double, -1, 3, 1, -1, 3> const&)> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>>&);
#endif
