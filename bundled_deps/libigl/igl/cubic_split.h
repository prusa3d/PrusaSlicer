#ifndef IGL_CUBIC_SPLIT_H
#define IGL_CUBIC_SPLIT_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// Splits a cubic Bézier curve defined by control points C at parameter t
  /// into two cubic Bézier curves defined by control points C1 and C2.
  /// C1 = [C0,C01,C012,C0123], C2 = [C0123,C123,C23,C3]
  ///
  /// @param[in] C  4 by dimensions matrix of control points for a cubic
  /// Bézier curve
  /// @param[in] t  Parameter at which to split curve
  /// @param[out] C01  1 by dim new control point
  /// @param[out] C012  1 by dim new control point
  /// @param[out] C0123  1 by dim new control point
  /// @param[out] C123  1 by dim new control point
  /// @param[out] C23  1 by dim new control point
  ///
  template <
    typename DerivedC,
    typename DerivedK>
  IGL_INLINE void cubic_split(
    const Eigen::MatrixBase<DerivedC>& C,
    const typename DerivedC::Scalar & t,
    Eigen::PlainObjectBase<DerivedK>& C01,
    Eigen::PlainObjectBase<DerivedK>& C012,
    Eigen::PlainObjectBase<DerivedK>& C0123,
    Eigen::PlainObjectBase<DerivedK>&  C123,
    Eigen::PlainObjectBase<DerivedK>&   C23);
  ///
  /// @param[out] C1  4 by dimensions matrix of control points for first cubic
  /// Bézier curve from C(0) to C(t)
  /// @param[out] C2  4 by dimensions matrix of control points for second cubic
  /// Bézier curve from C(t) to C(1)
  template <
    typename DerivedC,
    typename DerivedK>
  IGL_INLINE void cubic_split(
    const Eigen::MatrixBase<DerivedC>& C,
    const typename DerivedC::Scalar & t,
    Eigen::PlainObjectBase<DerivedK>& C1,
    Eigen::PlainObjectBase<DerivedK>& C2);
}

#ifndef IGL_STATIC_LIBRARY
#  include "cubic_split.cpp"
#endif

#endif

