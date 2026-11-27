#ifndef IGL_PREDICATES_CUBIC_WINDING_NUMBER_H
#define IGL_PREDICATES_CUBIC_WINDING_NUMBER_H
#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace predicates
  {
    /// Computes the (generalized) winding number of a cubic Bézier curve around a query.
    /// This implementation is similar to "Robust Containment Queries Over
    /// Collections of Rational Parametric Curves via Generalized Winding
    /// Numbers" [Spainhour et al. 2024].
    ///
    /// @param[in] C  4 by dimensions matrix of control points for a cubic
    /// Bézier curve
    /// @param[in] q  1 by dimensions query point
    template <typename DerivedC, typename Derivedq>
    IGL_INLINE typename DerivedC::Scalar cubic_winding_number(
      const Eigen::MatrixBase<DerivedC>& C,
      const Eigen::MatrixBase<Derivedq>& q);
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "cubic_winding_number.cpp"
#endif

#endif

