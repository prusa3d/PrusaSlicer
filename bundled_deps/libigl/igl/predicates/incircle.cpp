// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "incircle.h"
#include <predicates.h>

namespace igl {
namespace predicates {

using REAL = IGL_PREDICATES_REAL;
#include "IGL_PREDICATES_ASSERT_SCALAR.h"

template<typename Vector2D>
IGL_INLINE Orientation incircle(
    const Eigen::MatrixBase<Vector2D>& pa,
    const Eigen::MatrixBase<Vector2D>& pb,
    const Eigen::MatrixBase<Vector2D>& pc,
    const Eigen::MatrixBase<Vector2D>& pd)
{
  EIGEN_STATIC_ASSERT_VECTOR_SPECIFIC_SIZE(Vector2D, 2);
  IGL_PREDICATES_ASSERT_SCALAR(Vector2D);

  using Point = Eigen::Matrix<REAL, 2, 1>;
  Point a{pa[0], pa[1]};
  Point b{pb[0], pb[1]};
  Point c{pc[0], pc[1]};
  Point d{pd[0], pd[1]};

  const auto r = ::incircle(a.data(), b.data(), c.data(), d.data());

  if (r > 0) return Orientation::INSIDE;
  else if (r < 0) return Orientation::OUTSIDE;
  else return Orientation::COCIRCULAR;
}

}
}

#ifdef IGL_STATIC_LIBRARY
#define IGL_INCIRCLE(Vector) template igl::Orientation igl::predicates::incircle<Vector>(const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&, const Eigen::MatrixBase<Vector>&)
#define IGL_MATRIX(T, R, C) Eigen::Matrix<T, R, C>
IGL_INCIRCLE(IGL_MATRIX(float, 1, 2));
IGL_INCIRCLE(IGL_MATRIX(float, 2, 1));
#ifndef LIBIGL_PREDICATES_USE_FLOAT
IGL_INCIRCLE(IGL_MATRIX(double, 1, 2));
IGL_INCIRCLE(IGL_MATRIX(double, 2, 1));
#endif
#undef IGL_MATRIX
#undef IGL_INCIRCLE
#endif

