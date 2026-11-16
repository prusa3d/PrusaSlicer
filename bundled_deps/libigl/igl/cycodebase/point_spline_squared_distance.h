#ifndef IGL_CYCODEBASE_POINT_SPLINE_SQUARED_DISTANCE_H
#define IGL_CYCODEBASE_POINT_SPLINE_SQUARED_DISTANCE_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl {
  namespace cycodebase {
    /// Compute the squared distance from a set of points to a spline defined by
    /// cubic Bezier curves.
    ///
    /// @param[in] Q  #Q by dim matrix of query points
    /// @param[in] P  #P by dim matrix of spline control points
    /// @param[in] C  #C by 4 matrix of indices into P defining the cubic bezier
    /// curves making up the spline
    /// @param[out] sqrD  #Q vector of squared distances from each query point
    /// to the spline
    /// @param[out] I  #Q vector of indices of the closest cubic bezier curve in
    /// the spline to each query point
    /// @param[out] S  #Q vector of parametric locations on the closest cubic
    /// bezier curve of the closest point to each query point
    /// @param[out] K  #Q by dim matrix of closest point locations
    ///
    /// \see igl::cycodebase::point_cubic_squared_distance,
    /// igl::cycodebase::spline_eytzinger_aabb
    template <
      typename DerivedQ,
      typename DerivedP,
      typename DerivedC,
      typename DerivedsqrD,
      typename DerivedI,
      typename DerivedS,
      typename DerivedK>
    void point_spline_squared_distance(
      const Eigen::MatrixBase<DerivedQ>& Q,
      const Eigen::MatrixBase<DerivedP>& P,
      const Eigen::MatrixBase<DerivedC>& C,
      Eigen::PlainObjectBase<DerivedsqrD>& sqrD,
      Eigen::PlainObjectBase<DerivedI>& I,
      Eigen::PlainObjectBase<DerivedS>& S,
      Eigen::PlainObjectBase<DerivedK>& K);
    /// \brief overload with AABB acceleration structure
    ///
    /// @param[in] B1  #B by dim matrix of AABB min box corners
    /// @param[in] B2  #B by dim matrix of AABB max box corners
    /// @param[in] leaf  #B by 1 matrix of AABB leaf node indices/flags
    ///
    /// \see igl::eytzinger_aabb
    template <
      typename DerivedQ,
      typename DerivedP,
      typename DerivedC,
      typename DerivedB,
      typename Derivedleaf,
      typename DerivedsqrD,
      typename DerivedI,
      typename DerivedS,
      typename DerivedK>
    void point_spline_squared_distance(
      const Eigen::MatrixBase<DerivedQ>& Q,
      const Eigen::MatrixBase<DerivedP>& P,
      const Eigen::MatrixBase<DerivedC>& C,
      const Eigen::MatrixBase<DerivedB>& B1,
      const Eigen::MatrixBase<DerivedB>& B2,
      const Eigen::MatrixBase<Derivedleaf>& leaf,
      Eigen::PlainObjectBase<DerivedsqrD>& sqrD,
      Eigen::PlainObjectBase<DerivedI>& I,
      Eigen::PlainObjectBase<DerivedS>& S,
      Eigen::PlainObjectBase<DerivedK>& K);
  }
}

#ifndef IGL_STATIC_LIBRARY
  #include "point_spline_squared_distance.cpp"
#endif

#endif



