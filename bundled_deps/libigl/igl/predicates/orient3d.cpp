// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "orient3d.h"
#include "../parallel_for.h"
#include <predicates.h>
#include "exactinit.h"

namespace igl {
namespace predicates {

using REAL = IGL_PREDICATES_REAL;
#include "IGL_PREDICATES_ASSERT_SCALAR.h"

template<typename Vector3D>
IGL_INLINE Orientation orient3d(
    const Eigen::MatrixBase<Vector3D>& pa,
    const Eigen::MatrixBase<Vector3D>& pb,
    const Eigen::MatrixBase<Vector3D>& pc,
    const Eigen::MatrixBase<Vector3D>& pd)
{
  EIGEN_STATIC_ASSERT_VECTOR_SPECIFIC_SIZE(Vector3D, 3);
  IGL_PREDICATES_ASSERT_SCALAR(Vector3D);

  using Point = Eigen::Matrix<REAL, 3, 1>;
  Point a{pa[0], pa[1], pa[2]};
  Point b{pb[0], pb[1], pb[2]};
  Point c{pc[0], pc[1], pc[2]};
  Point d{pd[0], pd[1], pd[2]};

  const auto r = ::orient3d(a.data(), b.data(), c.data(), d.data());

  if (r > 0) return Orientation::POSITIVE;
  else if (r < 0) return Orientation::NEGATIVE;
  else return Orientation::COPLANAR;
}

template 
  <typename DerivedA,
   typename DerivedB,
   typename DerivedC,
   typename DerivedD,
   typename DerivedR>
IGL_INLINE void orient3d(
    const Eigen::MatrixBase<DerivedA>& A,
    const Eigen::MatrixBase<DerivedB>& B,
    const Eigen::MatrixBase<DerivedC>& C,
    const Eigen::MatrixBase<DerivedD>& D,
    Eigen::PlainObjectBase<DerivedR>& R)
{
  igl::predicates::exactinit();
  typedef typename DerivedR::Scalar RScalar;
  typedef typename DerivedA::Scalar Scalar;
  typedef Eigen::Matrix<Scalar, 1, 3> RowVector3S;

  // max(A.rows(),B.rows(),C.rows(),D.rows()) is the number of points
  const int np = std::max(
    std::max(A.rows(), B.rows()),
    std::max(C.rows(), D.rows()));
  R.resize(np, 1);
  igl::parallel_for(np, [&](const int p)
    {
      // Not sure if these copies are needed
      const RowVector3S a = A.row(p % A.rows());
      const RowVector3S b = B.row(p % B.rows());
      const RowVector3S c = C.row(p % C.rows());
      const RowVector3S d = D.row(p % D.rows());
      // Compute the orientation
      R(p) = static_cast<RScalar>(igl::predicates::orient3d(a, b, c, d));
    },1000);
}

}
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::predicates::orient3d<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1>>&);

#define IGL_ORIENT3D(Vector) template igl::Orientation igl::predicates::orient3d<Vector>(const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&)
#define IGL_MATRIX(T, R, C) Eigen::Matrix<T, R, C>
IGL_ORIENT3D(IGL_MATRIX(float, 1, 3));
IGL_ORIENT3D(IGL_MATRIX(float, 3, 1));
#ifndef LIBIGL_PREDICATES_USE_FLOAT
IGL_ORIENT3D(IGL_MATRIX(double, 1, 3));
IGL_ORIENT3D(IGL_MATRIX(double, 3, 1));
#endif
#undef IGL_MATRIX
#undef IGL_ORIENT3D

#endif
