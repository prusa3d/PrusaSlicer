#ifndef IGL_CYCODEBASE_POINT_CUBIC_SQUARED_DISTANCE_H
#define IGL_CYCODEBASE_POINT_CUBIC_SQUARED_DISTANCE_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl {
  namespace cycodebase {
    /// Compute the squared distance from a set of points to a cubic Bezier
    /// curve.
    ///
    /// @param[in] Q  #Q by dim matrix of query points
    /// @param[in] C  4 by dim matrix of control points defining the cubic
    /// bezier curve
    /// @param[out] sqrD  #Q vector of squared distances from each query point
    /// to the cubic bezier curve
    /// @param[out] S  #Q vector of parametric locations on the cubic bezier
    /// curve of the closest point to each query point
    /// @param[out] K  #Q by dim matrix of closest point locations
    ///
    /// \see igl::cubic_monomial_bases, igl::cycodebase::point_spline_squared_distance
    template <
      typename DerivedQ,
      typename DerivedC,
      typename DerivedsqrD,
      typename DerivedS,
      typename DerivedK>
    void point_cubic_squared_distance(
      const Eigen::MatrixBase<DerivedQ>& Q,
      const Eigen::MatrixBase<DerivedC>& C,
      Eigen::PlainObjectBase<DerivedsqrD>& sqrD,
      Eigen::PlainObjectBase<DerivedS>& S,
      Eigen::PlainObjectBase<DerivedK>& K);
    /// \brief overload
    ///
    /// @param[in] D 3 by dim matrix of monomial coefficients for dC/dt
    /// @param[in] B 6-vector of inner products of monomial coefficients of C
    /// and dCdt 
    template <
      typename DerivedQ,
      typename DerivedC,
      typename DerivedD,
      typename DerivedB,
      typename DerivedsqrD,
      typename DerivedS,
      typename DerivedK>
    void point_cubic_squared_distance(
      const Eigen::MatrixBase<DerivedQ>& Q,
      const Eigen::MatrixBase<DerivedC>& C,
      const Eigen::MatrixBase<DerivedD>& D,
      const Eigen::MatrixBase<DerivedB>& B,
      Eigen::PlainObjectBase<DerivedsqrD>& sqrD,
      Eigen::PlainObjectBase<DerivedS>& S,
      Eigen::PlainObjectBase<DerivedK>& K);
    /// \brief single point overload
    ///
    /// @param[in] q  dim vector of a query point
    template <
      typename Derivedq,
      typename DerivedC,
      typename DerivedD,
      typename DerivedB,
      typename Derivedk
      >
    void point_cubic_squared_distance(
      const Eigen::MatrixBase<Derivedq>& q,
      const Eigen::MatrixBase<DerivedC>& C,
      const Eigen::MatrixBase<DerivedD>& D,
      const Eigen::MatrixBase<DerivedB>& B,
      typename Derivedq::Scalar& sqrD,
      typename Derivedq::Scalar& s,
      Eigen::PlainObjectBase<Derivedk>& k);
    /// \brief single point overload
    template <
      typename Derivedq,
      typename DerivedC,
      typename Derivedk
      >
    void point_cubic_squared_distance(
      const Eigen::MatrixBase<Derivedq>& q,
      const Eigen::MatrixBase<DerivedC>& C,
      typename Derivedq::Scalar& sqrD,
      typename Derivedq::Scalar& s,
      Eigen::PlainObjectBase<Derivedk>& k);
  }
}

#ifndef IGL_STATIC_LIBRARY
  #include "point_cubic_squared_distance.cpp"
#endif

#endif


