#include "cubic_winding_number.h"
#include "point_in_convex_hull.h"
#include "orient2d.h"
#include "../Orientation.h"
#include "../cubic_is_flat.h"
#include "../cubic_split.h"
#include "../PI.h"
#include "../matlab_format.h"
#include <iostream>
#include <cstdio>
#include <cassert>

namespace 
{
  // helper where we assume that the input is not degenerate 
  template <typename DerivedC, typename Derivedq>
  inline typename DerivedC::Scalar cubic_winding_number_helper(
    const Eigen::MatrixBase<DerivedC>& C,
    const Eigen::MatrixBase<Derivedq>& q,
    const int depth)
  {
    using Scalar = typename DerivedC::Scalar;
    constexpr int max_depth = 50;
    assert(depth <= max_depth && "Exceeded max recursion depth in cubic_winding_number_helper");

    const auto res = 
      igl::predicates::point_in_convex_hull(
          q,
          C.row(0).eval(),
          C.row(1).eval(),
          C.row(2).eval(),
          C.row(3).eval());
    if( res == igl::Orientation::NEGATIVE || depth == max_depth)
    {
      const auto v0 = (C.row(0) - q).eval();
      const auto v3 = (C.row(3) - q).eval();
      const auto w = std::atan2(
        v0(0)*v3(1) - v0(1)*v3(0),
        v0(0)*v3(0) + v0(1)*v3(1)) /
        Scalar(2.0 * igl::PI);
      return w;
    }
    else
    {
      Eigen::Matrix<Scalar,4,2,Eigen::RowMajor> C1,C2;
      igl::cubic_split(C,Scalar(0.5),C1,C2);
      const auto w1 = cubic_winding_number_helper(C1,q,depth+1);
      const auto w2 = cubic_winding_number_helper(C2,q,depth+1);
      return w1 + w2;
    }
  }
}

template <typename DerivedC, typename Derivedq>
IGL_INLINE typename DerivedC::Scalar igl::predicates::cubic_winding_number(
  const Eigen::MatrixBase<DerivedC>& C,
  const Eigen::MatrixBase<Derivedq>& q)
{
  // Degenerate cases.
  using Scalar = typename DerivedC::Scalar;
  // check if C are all the same point
  if( (C.row(0)-C.row(1)).squaredNorm() == Scalar(0) &&
      (C.row(0)-C.row(2)).squaredNorm() == Scalar(0) &&
      (C.row(0)-C.row(3)).squaredNorm() == Scalar(0) )
  {
    return Scalar(0);
  }
  // Check if C are all COLLINEAR
  if( 
      (orient2d(C.row(0),C.row(3),C.row(1)) == Orientation::COLLINEAR) &&
      (orient2d(C.row(0),C.row(3),C.row(2)) == Orientation::COLLINEAR) )
  {
    const auto v0 = (C.row(0) - q).eval();
    const auto v3 = (C.row(3) - q).eval();
    const auto w = std::atan2(
      v0(0)*v3(1) - v0(1)*v3(0),
      v0(0)*v3(0) + v0(1)*v3(1)) /
      Scalar(2.0 * igl::PI);
    return w;
  }
  return cubic_winding_number_helper(C,q,0);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template Eigen::Matrix<double, 4, -1, 1, 4, -1>::Scalar igl::predicates::cubic_winding_number<Eigen::Matrix<double, 4, -1, 1, 4, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, 4, -1, 1, 4, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&);
template Eigen::Matrix<double, 4, 2, 0, 4, 2>::Scalar igl::predicates::cubic_winding_number<Eigen::Matrix<double, 4, 2, 0, 4, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>>(Eigen::MatrixBase<Eigen::Matrix<double, 4, 2, 0, 4, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&);
#endif
