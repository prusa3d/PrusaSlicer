#include "oriented_bounding_box.h"
#include <boost/range/has_range_iterator.hpp>
#include <CGAL/Aff_transformation_3.h>
//#include <CGAL/Simple_cartesian.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Optimal_bounding_box/oriented_bounding_box.h>

#include <array>
#include <iostream>

template <typename DerivedP, typename DerivedR>
  IGL_INLINE void igl::copyleft::cgal::oriented_bounding_box(
    const Eigen::MatrixBase<DerivedP>& P,
    Eigen::PlainObjectBase<DerivedR> & R)
{
  typedef typename DerivedP::Scalar Scalar;
  //typedef CGAL::Simple_cartesian<Scalar> K;
  typedef CGAL::Exact_predicates_inexact_constructions_kernel K;
  typedef typename K::Point_3 Point;
  std::vector<Point> points(P.rows());
  for (int i = 0; i < P.rows(); ++i)
  {
    points[i] = Point(P(i, 0), P(i, 1), P(i, 2));
  }

  std::array<Point, 8> obb_points;
  CGAL::Aff_transformation_3<K> affine;
  CGAL::oriented_bounding_box(
    points, 
    affine,
    CGAL::parameters::use_convex_hull(true));
  // Convert to Eigen affine transformation
  R.resize(3,3);
  for(int i = 0; i < 3; ++i)
  {
    for (int j = 0; j < 3; ++j)
    {
      R(i, j) = affine.m(i, j);
    }
  }
}

#ifdef IGL_STATIC_LIBRARY
// Explicit instantiation of template function
template void igl::copyleft::cgal::oriented_bounding_box<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, 3, 3, 0, 3, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, 3, 3, 0, 3, 3>>&);
#endif

