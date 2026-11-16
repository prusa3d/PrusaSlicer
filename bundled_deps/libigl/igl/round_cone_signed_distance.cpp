// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "round_cone_signed_distance.h"
#include "sign.h"

template <
  typename Derivedp,
  typename Deriveda, 
  typename Derivedb>
IGL_INLINE typename Derivedp::Scalar igl::round_cone_signed_distance(
  const Eigen::MatrixBase<Derivedp> & p, 
  const Eigen::MatrixBase<Deriveda> & a, 
  const Eigen::MatrixBase<Derivedb> & b, 
  const typename Derivedp::Scalar & r1, 
  const typename Derivedp::Scalar & r2)
{
  // https://iquilezles.org/articles/distfunctions/
  using Scalar = typename Derivedp::Scalar;
  using RowVector3S = Eigen::Matrix<Scalar, 1, 3>;
  // sampling independent computations (only depend on shape)
  const RowVector3S ba = b - a;
  const Scalar l2 = ba.dot(ba);
  const Scalar rr = r1 - r2;
  const Scalar a2 = l2 - rr*rr;
  const Scalar il2 = 1.0/l2;
  return round_cone_signed_distance(p,a,r1,r2,ba,l2,rr,a2,il2);
}

template <
  typename Derivedp,
  typename Deriveda, 
  typename Derivedba>
IGL_INLINE typename Derivedp::Scalar igl::round_cone_signed_distance(
  const Eigen::MatrixBase<Derivedp> & p, 
  const Eigen::MatrixBase<Deriveda> & a, 
  const typename Derivedp::Scalar & r1, 
  const typename Derivedp::Scalar & r2,
  const Eigen::MatrixBase<Derivedba> & ba,
  const typename Derivedp::Scalar & l2,
  const typename Derivedp::Scalar & rr,
  const typename Derivedp::Scalar & a2,
  const typename Derivedp::Scalar & il2)
{
  using Scalar = typename Derivedp::Scalar;
  using RowVector3S = Eigen::Matrix<Scalar, 1, 3>;
  // sampling dependant computations
  const RowVector3S pa = p - a;
  const Scalar y = pa.dot(ba);
  const Scalar z = y - l2;
  const Scalar x2 = ( pa*l2 - ba*y ).squaredNorm();
  const Scalar y2 = y*y*l2;
  const Scalar z2 = z*z*l2;

  // single square root!
  const Scalar k = sign(rr)*rr*rr*x2;
  if( sign(z)*a2*z2>k ) return  sqrt(x2 + z2)        *il2 - r2;
  if( sign(y)*a2*y2<k ) return  sqrt(x2 + y2)        *il2 - r1;
                        return (sqrt(x2*a2*il2)+y*rr)*il2 - r1;
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>::Scalar igl::round_cone_signed_distance<Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>, Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>, Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>>(Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>> const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>> const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>> const&, Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>::Scalar const&, Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3>, 1, 3, true>::Scalar const&);
template Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar igl::round_cone_signed_distance<Eigen::Matrix<double, 1, 3, 1, 1, 3>, Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3> const, 1, 3, true>, Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3> const, 1, 3, true>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3>> const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3> const, 1, 3, true>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar const&, Eigen::MatrixBase<Eigen::Block<Eigen::Matrix<double, 3, 3, 1, 3, 3> const, 1, 3, true>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar const&);
#endif
