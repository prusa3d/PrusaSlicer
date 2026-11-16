#include "roots.h"
#include <cyPolynomial.h>

namespace
{
  template <int MAX_DEG, typename Derivedcoef, typename DerivedR>
  int roots_helper(
      const int deg,
      const Eigen::MatrixBase<Derivedcoef>& coef,
      const typename Derivedcoef::Scalar xmin,
      const typename Derivedcoef::Scalar xmax,
      Eigen::PlainObjectBase<DerivedR>& R)
  {
    using cy::PolynomialRoots;
    if constexpr (MAX_DEG == 0)
    {
      // Should be impossible to reach here from roots()
      return 0;
    }
    else
    {
      if (deg == MAX_DEG)
      {
        using Scalar = typename Derivedcoef::Scalar;
        Scalar r[MAX_DEG];
        int nr = PolynomialRoots<MAX_DEG>(r, coef.derived().data(), xmin, xmax);
        // will resize if needed. Safe on compile-time sized matrices
        R.setConstant(MAX_DEG, std::numeric_limits<Scalar>::quiet_NaN());
        for (int i = 0; i < nr; ++i)
        {
          R[i] = r[i];
        }
        return nr;
      }
      return roots_helper<MAX_DEG - 1>(deg, coef, xmin, xmax, R);
    }
  }

}

template <
  typename Derivedcoef,
  typename DerivedR>
IGL_INLINE int igl::cycodebase::roots(
    const Eigen::MatrixBase<Derivedcoef>& coef,
    const typename Derivedcoef::Scalar xmin,
    const typename Derivedcoef::Scalar xmax,
    Eigen::PlainObjectBase<DerivedR>& R)
{
  using Scalar = typename Derivedcoef::Scalar;
  // Static assert that DerivedR::Scalar is same as Derivedcoef::Scalar
  static_assert(
      std::is_same<typename DerivedR::Scalar, Scalar>::value,
      "DerivedR::Scalar must be the same as Derivedcoef::Scalar");
  // Check that Derivedcoef and DerivedR are vectors        
  static_assert(
      (Derivedcoef::ColsAtCompileTime == 1 ||
       Derivedcoef::RowsAtCompileTime == 1) &&
      (DerivedR::ColsAtCompileTime == 1 ||
       DerivedR::RowsAtCompileTime == 1),
      "Derivedcoef and DerivedR must be vectors");

  constexpr int MAX_DEG = 16;
  const int deg = coef.size() - 1;

  if (deg < 1 || deg > MAX_DEG)
  {
    throw std::runtime_error("Polynomial degree out of range");
  }

  return ::roots_helper<MAX_DEG>( deg, coef, xmin, xmax, R);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template specialization
template IGL_INLINE int igl::cycodebase::roots<Eigen::RowVectorXd, Eigen::RowVectorXd>(
    const Eigen::MatrixBase<Eigen::RowVectorXd>& coef,
    const double xmin,
    const double xmax,
    Eigen::PlainObjectBase<Eigen::RowVectorXd>& R);
template int igl::cycodebase::roots<Eigen::Matrix<double, 4, 1, 0, 4, 1>, Eigen::Matrix<double, 3, 1, 0, 3, 1>>(Eigen::MatrixBase<Eigen::Matrix<double, 4, 1, 0, 4, 1>> const&, Eigen::Matrix<double, 4, 1, 0, 4, 1>::Scalar, Eigen::Matrix<double, 4, 1, 0, 4, 1>::Scalar, Eigen::PlainObjectBase<Eigen::Matrix<double, 3, 1, 0, 3, 1>>&);
template int igl::cycodebase::roots<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1>> const&, Eigen::Matrix<double, -1, 1, 0, -1, 1>::Scalar, Eigen::Matrix<double, -1, 1, 0, -1, 1>::Scalar, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1>>&);
#endif
