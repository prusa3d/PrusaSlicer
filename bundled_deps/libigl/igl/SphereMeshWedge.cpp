// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "SphereMeshWedge.h"
#include "round_cone_signed_distance.h"
#include "sign.h"
#include <cassert>
#include <Eigen/QR>
#include <Eigen/Geometry>

template <typename Scalar>
IGL_INLINE igl::SphereMeshWedge<Scalar>::SphereMeshWedge(
  const RowVector3S & V0,
  const RowVector3S & V1,
  const RowVector3S & V2,
  const Scalar r0,
  const Scalar r1,
  const Scalar r2)
{
  // Internal copy
  V.row(0) = V0;
  V.row(1) = V1;
  V.row(2) = V2;
  r(0) = r0;
  r(1) = r1;
  r(2) = r2;

  flavor = FULL;
  // By default use full

  EV.row(0) = V.row(2) - V.row(1);
  EV.row(1) = V.row(0) - V.row(2);
  EV.row(2) = V.row(1) - V.row(0);
  l = EV.rowwise().norm();
  l2 = l.array().square();
  rr << r(1) - r(2), r(2) - r(0), r(0) - r(1);
  a2 = l2.array() - rr.array().square();
  il2 = 1.0/l2.array();

  /////////////////////////////////////////////
  /// BIG_VERTEX ?
  /////////////////////////////////////////////
  {
    r.maxCoeff(&max_i);
    int j = (max_i+1)%3;
    int k = (max_i+2)%3;
    if((l(k) + r(j) < r(max_i)) && (l(j) + r(k) < r(max_i)))
    {
      flavor = BIG_VERTEX;
    }
  }

  /////////////////////////////////////////////
  /// BIG_EDGE ?
  /////////////////////////////////////////////
  if(flavor == FULL)
  {
    // Case where one edge's roundCone containes the others
    for(int e = 0;e<3;e++)
    {
      const int i = (e+1)%3;
      const int j = (e+2)%3;
      const int k = (e+3)%3;
      const Scalar s = 
        igl::round_cone_signed_distance(V.row(k),V.row(i),V.row(j),r(i),r(j));
      if(-s > r(k))
      {
        flavor = BIG_EDGE;
        max_i = i;
        break;
      }
    }
  }

  if(flavor == FULL && !compute_planes())
  {
    flavor = NO_TRIANGLE;
  }
}

template <typename Scalar>
IGL_INLINE Scalar igl::SphereMeshWedge<Scalar>::operator()(const RowVector3S & p) const
{
  if(flavor == BIG_VERTEX)
  {
    // Case 0: Vertex i
    return (p - V.row(max_i)).norm() - r(max_i);
  }

  if(flavor == BIG_EDGE)
  {
    const int i = max_i;
    const int j = (i+1)%3;
    // Case 1: Edge e
    return this->round_cone_signed_distance(p,i,j);
  }

  Scalar s = std::numeric_limits<Scalar>::infinity();
  if(flavor == FULL)
  {
    // This is possibly the bottleneck and could be turned into precomputed
    // plane equations.

    // signed distance to triangle plane (this is immediately recomputed later in
    // sdSkewedExtrudedTriangle...)
    const auto plane_sdf = [](
        const RowVector3S & p,
        const Eigen::RowVector4d & plane)
    {
      return plane.head<3>().dot(p) + plane(3);
    };
    Scalar d0 = plane_sdf(p, planes.row(0));
    Scalar planes_s = -std::abs(d0);
    // Reflect if necessary so that q is always on negative side of plane
    RowVector3S q = p - (d0 - planes_s) * planes.row(0).template head<3>();
    // Other planes (for negative side slab, by symmetry)
    for(int i = 1;i<planes.rows();i++)
    {
      planes_s = std::max(planes_s,plane_sdf(q, planes.row(i)));
    }
    // This produces correct interior distance
    if(planes_s <= 0)
    {
      s = std::min(s,planes_s);
    }else
    {

      const auto & nor = planes.row(1).template head<3>();
      const RowVector3S q0 = q - T.row(0);
      const RowVector3S q1 = q - T.row(1);
      const RowVector3S q2 = q - T.row(2);

      if(!(sign(C.row(0).dot(q0)) + 
           sign(C.row(1).dot(q1)) + 
           sign(C.row(2).dot(q2))<2.0))
      {
        s = std::min(s,planes_s);
      }
    }

    //s = std::min(s,sdSkewedExtrudedTriangle(q,V,T));
    //s = std::min(s,sdSkewedExtrudedTriangle(p,B,V));
  }
  assert(flavor == FULL || flavor == NO_TRIANGLE);

  for(int e = 0;e<3;e++)
  {
    const int i = (e+1)%3;
    const int j = (e+2)%3;
    s = std::min(s,this->round_cone_signed_distance(p,i,j));
  }
  return s;
}

template <typename Scalar>
IGL_INLINE bool igl::SphereMeshWedge<Scalar>::compute_planes()
{
  // Non-degenerate case
  const RowVector3S & a = V.row(0);
  const RowVector3S & b = V.row(1);
  const RowVector3S & c = V.row(2);
  const Scalar & ra = r(0);
  const Scalar & rb = r(1);
  const Scalar & rc = r(2);
  Eigen::Matrix<Scalar,2,3,Eigen::RowMajor> A;
  A<<
    b-a,
    c-a;
  const Eigen::Vector2d d(rb-ra,rc-ra);
  const RowVector3S N = (A.row(0).cross(A.row(1))).normalized();
  //const Eigen::CompleteOrthogonalDecomposition<decltype(A)> cod(A);
  const RowVector3S n0 = A.completeOrthogonalDecomposition().solve(d);
  const Scalar qA = N.squaredNorm();
  // qB is zeros by construction. We could delete all terms involving qB
  // It's not even clear if keeping them would lead to more accurate results.
  const Scalar qB = 2 * N.dot(n0);
  const Scalar qC = n0.squaredNorm() - 1;
  const Scalar qD = qB*qB - 4*qA*qC;

  if(qD<0) { return false; }

  Scalar t_sol_1 = (-qB + std::sqrt(qD)) / (2*qA);
  RowVector3S n1 = -(t_sol_1 * N + n0);
  T  = V + r * n1;

  const auto plane_equation = [](
      const RowVector3S & a,
      const RowVector3S & b,
      const RowVector3S & c)->Eigen::RowVector4d
  {
    RowVector3S n = (b-a).cross(c-a).normalized();
    n.normalize();
    Scalar d = -n.dot(a);
    return Eigen::RowVector4d(n(0),n(1),n(2),d);
  };

  planes.row(0) = plane_equation(V.row(0),V.row(1),V.row(2));
  planes.row(1) = plane_equation(T.row(2),T.row(1),T.row(0));
  planes.row(2) = plane_equation(V.row(1),V.row(0),T.row(0));
  planes.row(3) = plane_equation(V.row(2),V.row(1),T.row(1));
  planes.row(4) = plane_equation(V.row(0),V.row(2),T.row(2));

  // Determine if the closest point is on the face.
  const RowVector3S v10 = T.row(1) - T.row(0); 
  const RowVector3S v21 = T.row(2) - T.row(1); 
  const RowVector3S v02 = T.row(0) - T.row(2); 
  const auto & nor = planes.row(1).template head<3>();
  const RowVector3S c10 = v10.cross(nor);
  const RowVector3S c21 = v21.cross(nor);
  const RowVector3S c02 = v02.cross(nor);
  C<<c10,c21,c02;
  return true;
}

template <typename Scalar>
IGL_INLINE Scalar igl::SphereMeshWedge<Scalar>::round_cone_signed_distance(const RowVector3S & p, const int i, const int j) const
{
  const int e = (j+1)%3;
  return igl::round_cone_signed_distance(
    p, V.row(i), r(i), r(j), EV.row(e), l2(e), rr(e), a2(e), il2(e));
}


#ifdef IGL_STATIC_LIBRARY
/// Explicit template instantiation
template class igl::SphereMeshWedge<double>;
#endif

