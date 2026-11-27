#ifndef IGL_PREDICATES_SPLINE_WINDING_NUMBER_H
#define IGL_PREDICATES_SPLINE_WINDING_NUMBER_H
#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace predicates
  {
    /// Computes the (generalized) winding number of a spline of cubic Bézier
    /// curves around a set of query points.
    ///
    /// @param[in] q  1 by dim query point
    /// @param[in] P  #P by dim matrix of spline control points
    /// @param[in] C  #C by 4 matrix of indices into P defining the cubic Bézier
    /// curves making up the spline
    /// @param[in] B1  #B by dim matrix of AABB min box corners 
    /// @param[in] B2  #B by dim matrix of AABB max box corners 
    /// @param[in] leaf  #B by 1 matrix of AABB leaf node indices/flags
    /// @param[in] Q  #Q by dim matrix of query points
    /// @param[out] W  #Q by 1 matrix of winding numbers for each query point
    ///
    ///
    /// \see igl::cycodebase::box_cubic, igl::predicates::cubic_winding_number,
    /// igl::eytzinger_aabb, igl::cycodebase::spline_eytzinger_aabb
    template <
      typename DerivedP, 
      typename DerivedC, 
      typename DerivedB,
      typename Derivedleaf,
      typename DerivedQ,
      typename DerivedW>
    IGL_INLINE void spline_winding_number(
      const Eigen::MatrixBase<DerivedP>& P,
      const Eigen::MatrixBase<DerivedC>& C,
      const Eigen::MatrixBase<DerivedB>& B1,
      const Eigen::MatrixBase<DerivedB>& B2,
      const Eigen::MatrixBase<Derivedleaf>& leaf,
      const Eigen::MatrixBase<DerivedQ> & Q,
      Eigen::PlainObjectBase<DerivedW>& W);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "spline_winding_number.cpp"
#endif

#endif


