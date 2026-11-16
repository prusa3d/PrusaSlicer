#ifndef IGL_CYCODEBASE_ROOTS_H
#define IGL_CYCODEBASE_ROOTS_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl {
  namespace cycodebase {
    /// Compute the real roots of a polynomial with given coefficients within a
    /// given interval [xmin, xmax].
    ///
    /// @param[in] coef  Vector of polynomial coefficients in increasing order
    /// of degree (i.e., coef[0] + coef[1]*x + coef[2]*x^2 + ... )
    /// @param[in] xmin  Minimum x value of the interval
    /// @param[in] xmax  Maximum x value of the interval
    /// @param[out] R  Vector of real roots within [xmin, xmax]
    /// @return  Number of real roots found within [xmin, xmax]
    template <
      typename Derivedcoef,
      typename DerivedR>
    IGL_INLINE int roots(
        const Eigen::MatrixBase<Derivedcoef>& coef,
        const typename Derivedcoef::Scalar xmin,
        const typename Derivedcoef::Scalar xmax,
        Eigen::PlainObjectBase<DerivedR>& R);
  }
}

#ifndef IGL_STATIC_LIBRARY
  #include "roots.cpp"
#endif

#endif
