// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "orient2d.h"
#include "exactinit.h"
#include "../parallel_for.h"
#include <predicates.h>

namespace igl {
namespace predicates {

using REAL = IGL_PREDICATES_REAL;
#include "IGL_PREDICATES_ASSERT_SCALAR.h"

template <
      typename Derivedpa,
      typename Derivedpb,
      typename Derivedpc>
IGL_INLINE Orientation orient2d(
    const Eigen::MatrixBase<Derivedpa>& pa,
    const Eigen::MatrixBase<Derivedpb>& pb,
    const Eigen::MatrixBase<Derivedpc>& pc)
{
  static_assert(
      (Derivedpa::RowsAtCompileTime == 2 && Derivedpa::ColsAtCompileTime == 1) ||
      (Derivedpa::RowsAtCompileTime == 1 && Derivedpa::ColsAtCompileTime == 2) ||
      (Derivedpa::RowsAtCompileTime == Eigen::Dynamic && Derivedpa::ColsAtCompileTime == 1 ) ||
      (Derivedpa::RowsAtCompileTime == 1 && Derivedpa::ColsAtCompileTime == Eigen::Dynamic ),
      "pa must be a 2D point");
  assert(pa.size() == 2 && "pa must be a 2D point");
  static_assert(
      (Derivedpb::RowsAtCompileTime == 2 && Derivedpb::ColsAtCompileTime == 1) ||
      (Derivedpb::RowsAtCompileTime == 1 && Derivedpb::ColsAtCompileTime == 2) ||
      (Derivedpb::RowsAtCompileTime == Eigen::Dynamic && Derivedpb::ColsAtCompileTime == 1 ) ||
      (Derivedpb::RowsAtCompileTime == 1 && Derivedpb::ColsAtCompileTime == Eigen::Dynamic ),
      "pb must be a 2D point");
  assert(pb.size() == 2 && "pb must be a 2D point");
  static_assert(
      (Derivedpc::RowsAtCompileTime == 2 && Derivedpc::ColsAtCompileTime == 1) ||
      (Derivedpc::RowsAtCompileTime == 1 && Derivedpc::ColsAtCompileTime == 2) ||
      (Derivedpc::RowsAtCompileTime == Eigen::Dynamic && Derivedpc::ColsAtCompileTime == 1 ) ||
      (Derivedpc::RowsAtCompileTime == 1 && Derivedpc::ColsAtCompileTime == Eigen::Dynamic ),
      "pc must be a 2D point");
  assert(pc.size() == 2 && "pc must be a 2D point");



  using Point = Eigen::Matrix<REAL, 2, 1>;
  Point a{pa[0], pa[1]};
  Point b{pb[0], pb[1]};
  Point c{pc[0], pc[1]};

  const auto r = ::orient2d(a.data(), b.data(), c.data());

  if (r > 0) return Orientation::POSITIVE;
  else if (r < 0) return Orientation::NEGATIVE;
  else return Orientation::COLLINEAR;
}

template 
  <typename DerivedA,
   typename DerivedB,
   typename DerivedC,
   typename DerivedR>
IGL_INLINE void orient2d(
    const Eigen::MatrixBase<DerivedA>& A,
    const Eigen::MatrixBase<DerivedB>& B,
    const Eigen::MatrixBase<DerivedC>& C,
    Eigen::PlainObjectBase<DerivedR>& R)
{
  igl::predicates::exactinit();
  typedef typename DerivedR::Scalar RScalar;
  typedef typename DerivedA::Scalar Scalar;
  typedef Eigen::Matrix<Scalar, 1, 2> RowVector2S;

  // max(A.rows(),B.rows(),C.rows()) is the number of points
  const int np = std::max(
    std::max(A.rows(), B.rows()),C.rows());
  R.resize(np, 1);
  igl::parallel_for(np, [&](const int p)
    {
      // Not sure if these copies are needed
      const RowVector2S a = A.row(p % A.rows());
      const RowVector2S b = B.row(p % B.rows());
      const RowVector2S c = C.row(p % C.rows());
      // Compute the orientation
      R(p) = static_cast<RScalar>(igl::predicates::orient2d(a, b, c));
    },1000);
}

}
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template igl::Orientation igl::predicates::orient2d<Eigen::Block<Eigen::Matrix<double, 4, -1, 1, 4, -1> const, 1, -1, true>, Eigen::Block<Eigen::Matrix<double, 4, -1, 1, 4, -1> const, 1, -1, true>, Eigen::Block<Eigen::Matrix<double, 4, -1, 1, 4, -1> const, 1, -1, true>>(Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 4, -1, 1, 4, -1> const, 1, -1, true>> const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 4, -1, 1, 4, -1> const, 1, -1, true>> const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 4, -1, 1, 4, -1> const, 1, -1, true>> const&);
template igl::Orientation igl::predicates::orient2d<Eigen::Block<Eigen::Matrix<double, 4, 2, 0, 4, 2> const, 1, 2, false>, Eigen::Block<Eigen::Matrix<double, 4, 2, 0, 4, 2> const, 1, 2, false>, Eigen::Block<Eigen::Matrix<double, 4, 2, 0, 4, 2> const, 1, 2, false>>(Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 4, 2, 0, 4, 2> const, 1, 2, false>> const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 4, 2, 0, 4, 2> const, 1, 2, false>> const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 4, 2, 0, 4, 2> const, 1, 2, false>> const&);
template igl::Orientation igl::predicates::orient2d<Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&);
template igl::Orientation igl::predicates::orient2d<Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, -1, 1, 1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&);
template igl::Orientation igl::predicates::orient2d<Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&);
template igl::Orientation igl::predicates::orient2d<Eigen::Matrix<double, 2, 1, 0, 2, 1>, Eigen::Matrix<double, 2, 1, 0, 2, 1>, Eigen::Matrix<double, 2, 1, 0, 2, 1>>(Eigen::MatrixBase<Eigen::Matrix<double, 2, 1, 0, 2, 1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 2, 1, 0, 2, 1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 2, 1, 0, 2, 1>> const&);
#endif
