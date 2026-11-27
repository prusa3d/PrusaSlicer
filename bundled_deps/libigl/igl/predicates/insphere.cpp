// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "insphere.h"
#include <predicates.h>

namespace igl {
namespace predicates {

using REAL = IGL_PREDICATES_REAL;
#include "IGL_PREDICATES_ASSERT_SCALAR.h"

template<typename Vector3D>
IGL_INLINE Orientation insphere(
    const Eigen::MatrixBase<Vector3D>& pa,
    const Eigen::MatrixBase<Vector3D>& pb,
    const Eigen::MatrixBase<Vector3D>& pc,
    const Eigen::MatrixBase<Vector3D>& pd,
    const Eigen::MatrixBase<Vector3D>& pe)
{
  EIGEN_STATIC_ASSERT_VECTOR_SPECIFIC_SIZE(Vector3D, 3);
  IGL_PREDICATES_ASSERT_SCALAR(Vector3D);

  using Point = Eigen::Matrix<REAL, 3, 1>;
  Point a{pa[0], pa[1], pa[2]};
  Point b{pb[0], pb[1], pb[2]};
  Point c{pc[0], pc[1], pc[2]};
  Point d{pd[0], pd[1], pd[2]};
  Point e{pe[0], pe[1], pe[2]};

  const auto r = ::insphere(a.data(), b.data(), c.data(), d.data(), e.data());

  if (r > 0) return Orientation::INSIDE;
  else if (r < 0) return Orientation::OUTSIDE;
  else return Orientation::COSPHERICAL;
}


}
}

#ifdef IGL_STATIC_LIBRARY
#define IGL_INSPHERE(Vector) template igl::Orientation igl::predicates::insphere<Vector>(const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&)
#define IGL_MATRIX(T, R, C) Eigen::Matrix<T, R, C>
IGL_INSPHERE(IGL_MATRIX(float, 1, 3));
IGL_INSPHERE(IGL_MATRIX(float, 3, 1));
#ifndef LIBIGL_PREDICATES_USE_FLOAT
IGL_INSPHERE(IGL_MATRIX(double, 1, 3));
IGL_INSPHERE(IGL_MATRIX(double, 3, 1));
#endif
#undef IGL_MATRIX
#undef IGL_INSPHERE
#endif


