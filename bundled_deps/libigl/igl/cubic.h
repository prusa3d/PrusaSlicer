#ifndef IGL_CUBIC_H
#define IGL_CUBIC_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// Evaluate a cubic Bézier curve defined by control points C at parameter t.
  ///
  /// @param[in] C  4 by dimensions matrix of control points for a cubic
  /// Bézier curve
  /// @param[in] t  Parameter at which to evaluate curve
  /// @param[out] P  1 by dimensions point on curve C(t)
  template
    <
    typename  DerivedC,
    typename  DerivedP>
  IGL_INLINE void cubic(
    const Eigen::MatrixBase<DerivedC>& C,
    const typename DerivedP::Scalar & t,
    Eigen::PlainObjectBase<DerivedP>& P);
}

#ifndef IGL_STATIC_LIBRARY
#  include "cubic.cpp"
#endif

#endif
