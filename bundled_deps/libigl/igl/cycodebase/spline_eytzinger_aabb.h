#ifndef IGL_CYCODEBASE_SPLINE_EYTZINGER_AABB_H
#define IGL_CYCODEBASE_SPLINE_EYTZINGER_AABB_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl {
  namespace cycodebase {
    /// Compute an AABB acceleration structure for a spline of cubic Bezier
    /// curves in Eytzinger layout.
    ///
    /// @param[in] P  #P by dim matrix of spline control points
    /// @param[in] C  #C by 4 matrix of indices into P defining the cubic Bézier
    /// curves making up the spline
    /// @param[out] B1  #B by dim matrix of AABB min box corners
    /// @param[out] B2  #B by dim matrix of AABB max box corners
    /// @param[out] leaf  #B by 1 matrix of AABB leaf node indices/flags
    ///
    /// \see igl::cycodebase::box_cubic, igl::eytzinger_aabb
    ///
    template <
      typename DerivedP,
      typename DerivedC,
      typename DerivedB,
      typename Derivedleaf>
    void spline_eytzinger_aabb(
      const Eigen::MatrixBase<DerivedP>& P,
      const Eigen::MatrixBase<DerivedC>& C,
      Eigen::PlainObjectBase<DerivedB>& B1,
      Eigen::PlainObjectBase<DerivedB>& B2,
      Eigen::PlainObjectBase<Derivedleaf>& leaf);
  }
}

#ifndef IGL_STATIC_LIBRARY
  #include "spline_eytzinger_aabb.cpp"
#endif

#endif




