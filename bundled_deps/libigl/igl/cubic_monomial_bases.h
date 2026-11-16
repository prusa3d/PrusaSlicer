#ifndef IGL_CUBIC_MONOMIAL_BASES_H
#define IGL_CUBIC_MONOMIAL_BASES_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// Compute monomial basis representations for cubic Bézier curves.
  ///
  /// @param[in]  C  4 by dimensions matrix of control points for a cubic
  /// Bézier curve
  /// @param[out] M  4 by dimensions matrix of monomial coefficients for C(t)
  /// @param[out] D  3 by dimensions matrix of monomial coefficients for dC/dt
  /// @param[out] B  6-vector of inner products of those basis functions for C(t)
  ///
  /// \see igl::cycodebase::point_cubic_squared_distance
  template <
    typename DerivedC,
    typename DerivedM,
    typename DerivedD,
    typename DerivedB>
  IGL_INLINE void cubic_monomial_bases(
    const Eigen::MatrixBase<DerivedC>& C,
    Eigen::PlainObjectBase<DerivedM>& M,
    Eigen::PlainObjectBase<DerivedD>& D,
    Eigen::PlainObjectBase<DerivedB>& B);
}

#ifndef IGL_STATIC_LIBRARY
#  include "cubic_monomial_bases.cpp"
#endif

#endif
