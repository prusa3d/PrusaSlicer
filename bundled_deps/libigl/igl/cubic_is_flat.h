#ifndef IGL_CUBIC_IS_FLAT_H
#define IGL_CUBIC_IS_FLAT_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// "Piecewise Linear Approximation of Bézier Curves" [Fischer 2000]. The test
  /// computes a scalar and checks if it is less than 16 times
  /// `squared_distance_bound`. If so, the curve's maximum squared distance to
  /// the chord from first to last point will be less than
  /// `squared_distance_bound`. The test is well behaved for degenerate input
  /// (e.g., all rows in C are the same point).
  ///
  /// @param[in] C  4 by dimensions matrix of control points for a cubic
  /// Bézier curve
  /// @param[in] squared_distance_bound  Squared distance tolerance
  /// @return  True if the curve is flat within the given tolerance
  template <typename DerivedC>
  IGL_INLINE bool cubic_is_flat(
    const Eigen::MatrixBase<DerivedC>& C,
    const typename DerivedC::Scalar squared_distance_bound);
}

#ifndef IGL_STATIC_LIBRARY
#  include "cubic_is_flat.cpp"
#endif

#endif
