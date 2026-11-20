#include "super_fibonacci.h"
#include "PI.h"
#include <cmath>

template <typename DerivedQ>
IGL_INLINE void igl::super_fibonacci(
  const int n,
  Eigen::PlainObjectBase<DerivedQ> & Q)
{
  typedef typename DerivedQ::Scalar Scalar;
  // https://marcalexa.github.io/superfibonacci/
  Scalar dn = 1.0 / (Scalar)n;
  Scalar mc0 = 1.0 / std::sqrt(2.0);
  Scalar mc1 = 1.0 / 1.533751168755204288118041;

  Q.resize(n,4);
  for (int i = 0; i < n; i++)
  {
    Scalar s = (Scalar)i+0.5;
    Scalar ab = 2.0 * igl::PI * s;
    Scalar alpha = ab * mc0;
    Scalar beta = ab * mc1;
    s *= dn;
    Scalar r = std::sqrt(s);
    Scalar R = std::sqrt(1.0-s);
    Q(i,0) = r*std::sin(alpha);
    Q(i,1) = r*std::cos(alpha);
    Q(i,2) = R*std::sin(beta);
    Q(i,3) = R*std::cos(beta);
  }
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::super_fibonacci<Eigen::Matrix<double, -1, 4, 1, -1, 4>>(int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 4, 1, -1, 4>>&);
template void igl::super_fibonacci<Eigen::Matrix<double, -1, -1, 0, -1, -1>>(int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>>&);
#endif
