#include "point_cubic_squared_distance.h"
#include "../parallel_for.h"
#include "../cubic.h"
#include "../cubic_monomial_bases.h"
#include <cyPolynomial.h>
#include <limits>

template <
  typename DerivedQ,
  typename DerivedC,
  typename DerivedsqrD,
  typename DerivedS,
  typename DerivedK>
void igl::cycodebase::point_cubic_squared_distance(
  const Eigen::MatrixBase<DerivedQ>& Q,
  const Eigen::MatrixBase<DerivedC>& C,
  Eigen::PlainObjectBase<DerivedsqrD>& sqrD,
  Eigen::PlainObjectBase<DerivedS>& S,
  Eigen::PlainObjectBase<DerivedK>& K)
{
  using Scalar = typename DerivedQ::Scalar;
  constexpr int ColsAtCompileTime = DerivedQ::ColsAtCompileTime;
  const int dim = C.cols();
  assert(C.rows() == 4 && "C should be 4 x dim.");
  assert(Q.cols() == dim && "Q and C should have the same number of columns.");
  // min_t ‖ Q - C(t) ‖^2
  //
  // Necessary condition for a minimum:
  //
  // d/dt ‖ Q - C(t) ‖^2 = 0
  // => d/dt (Q - C(t)) · (Q - C(t)) = 0
  // => -2 (dC/dt) · (Q - C(t)) = 0
  // => (dC/dt) · (C(t) - Q) = f(t) = 0
  //
  using RowVectorSD = Eigen::RowVector<Scalar,ColsAtCompileTime>;
  using MatrixS4D = Eigen::Matrix<Scalar,4,ColsAtCompileTime>;
  using MatrixS3D = Eigen::Matrix<Scalar,3,ColsAtCompileTime>;
  MatrixS4D M;
  MatrixS3D D;
  Eigen::RowVector<Scalar,6> B;
  cubic_monomial_bases(C, M, D, B);
  return point_cubic_squared_distance( Q, C, D, B, sqrD, S, K);
}

template <
  typename DerivedQ,
  typename DerivedC,
  typename DerivedD,
  typename DerivedB,
  typename DerivedsqrD,
  typename DerivedS,
  typename DerivedK>
void igl::cycodebase::point_cubic_squared_distance(
  const Eigen::MatrixBase<DerivedQ>& Q,
  const Eigen::MatrixBase<DerivedC>& C,
  const Eigen::MatrixBase<DerivedD>& D,
  const Eigen::MatrixBase<DerivedB>& B,
  Eigen::PlainObjectBase<DerivedsqrD>& sqrD,
  Eigen::PlainObjectBase<DerivedS>& S,
  Eigen::PlainObjectBase<DerivedK>& K)
{
  // static assert that C has 4 rows or Dynamic
  static_assert(
    DerivedC::RowsAtCompileTime == 4 ||
    DerivedC::RowsAtCompileTime == Eigen::Dynamic,
    "C must have 4 rows.");
  // runtime assert that C has 4 rows
  assert(C.rows() == 4 && "C must have 4 rows.");
  using Scalar = typename DerivedQ::Scalar;
  constexpr int ColsAtCompileTime = DerivedQ::ColsAtCompileTime;
  const int dim = C.cols();
  using RowVectorSD = Eigen::RowVector<Scalar,ColsAtCompileTime>;
  sqrD.setConstant(Q.rows(),1,std::numeric_limits<Scalar>::infinity());
  S.resize(Q.rows());
  K.resize(Q.rows(),dim);
  const int n = Q.rows();
  igl::parallel_for(n,[&](const int i)
  {
    RowVectorSD k_i;
    const RowVectorSD q_i = Q.row(i);
    point_cubic_squared_distance( q_i, C, D, B, sqrD(i), S(i), k_i);
    K.row(i) = k_i;
  }
  ,1000);
}

template <
  typename Derivedq,
  typename DerivedC,
  typename DerivedD,
  typename DerivedB,
  typename Derivedk
  >
void igl::cycodebase::point_cubic_squared_distance(
  const Eigen::MatrixBase<Derivedq>& q,
  const Eigen::MatrixBase<DerivedC>& C,
  const Eigen::MatrixBase<DerivedD>& D,
  const Eigen::MatrixBase<DerivedB>& B,
  typename Derivedq::Scalar& sqrD,
  typename Derivedq::Scalar& s,
  Eigen::PlainObjectBase<Derivedk>& k)
{
  using cy::PolynomialRoots;
  using Scalar = typename Derivedq::Scalar;
  const int dim = C.cols();
  // static assert that C has 4 rows or Dynamic
  static_assert(
    DerivedC::RowsAtCompileTime == 4 ||
    DerivedC::RowsAtCompileTime == Eigen::Dynamic,
    "C must have 4 rows.");
  assert(C.rows() == 4 && "C should be 4 x dim.");
  assert(q.cols() == dim && "q and C should have the same number of columns.");
  typedef Eigen::Matrix<Scalar,1,Derivedq::ColsAtCompileTime> RowVectorSD;
  // Fill in coefficients using precomputed data and Q
  // => (dC/dt) · (C(t) - Q) = f(t) = 0
  //
  // C(t) = (1-t)^3 C0 + 3(1-t)^2 t C1 + 3(1-t) t^2 C2 + t^3 C3
  // C(t) = C0 + t⋅3(C1 - C0) + t^2⋅3(C0 - 2C1 + C2) + t^3⋅(C3 - C0 + 3(C1 - C2))
  // dC/dt =       3(C1 - C0) +   t⋅6(C0 - 2C1 + C2) + t^2⋅3(C3 - C0 + 3(C1 - C2))
  Eigen::RowVector<Scalar,6> coef = B;
  for (int j = 0; j < 3; ++j)
  {
    coef(j) -= D.row(j).dot(q);
  }

  sqrD = std::numeric_limits<Scalar>::infinity();
  // f is a quintic polynomial:
  constexpr int N = 5;
  Scalar r[N];
  int nr = PolynomialRoots<N>(r, coef.data(),Scalar(0),Scalar(1));
  for(int j = 0;j<nr+2;j++)
  {
    Scalar t;
    if(j==nr)
    {
      t = Scalar(0);
    }
    else if(j==nr+1)
    {
      t = Scalar(1);
    }else
    {
      t = r[j];
    }
    RowVectorSD Ct;
    igl::cubic(C,t,Ct);
    const Scalar sqrD_j = (Ct - q).squaredNorm();
    if(sqrD_j < sqrD)
    {
      sqrD = sqrD_j;
      s = t;
      k = Ct;
    }
  }
}

template <
  typename Derivedq,
  typename DerivedC,
  typename Derivedk
  >
void igl::cycodebase::point_cubic_squared_distance(
  const Eigen::MatrixBase<Derivedq>& q,
  const Eigen::MatrixBase<DerivedC>& C,
  typename Derivedq::Scalar& sqrD,
  typename Derivedq::Scalar& s,
  Eigen::PlainObjectBase<Derivedk>& k)
{
  using Scalar = typename Derivedq::Scalar;
  constexpr int ColsAtCompileTime = Derivedq::ColsAtCompileTime;
  static_assert(
    DerivedC::RowsAtCompileTime == 4 ||
    DerivedC::RowsAtCompileTime == Eigen::Dynamic,
    "C must have 4 rows.");
  static_assert(
    int(Derivedq::ColsAtCompileTime) == int(DerivedC::ColsAtCompileTime),
    "q and C must have the same number of columns.");
  const int dim = C.cols();
  assert(C.rows() == 4 && "C should be 4 x dim.");
  assert(q.cols() == dim && "q and C should have the same number of columns.");
  using RowVectorSD = Eigen::RowVector<Scalar,ColsAtCompileTime>;
  using MatrixS4D = Eigen::Matrix<Scalar,4,ColsAtCompileTime>;
  using MatrixS3D = Eigen::Matrix<Scalar,3,ColsAtCompileTime>;
  MatrixS4D M;
  MatrixS3D D;
  Eigen::RowVector<Scalar,6> B;
  cubic_monomial_bases(C, M, D, B);
  return point_cubic_squared_distance( q, C, D, B, sqrD, s, k);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::cycodebase::point_cubic_squared_distance<Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 4, -1, 0, 4, -1>, Eigen::Matrix<double, 3, -1, 0, 3, -1>, Eigen::Matrix<double, 6, 1, 0, 6, 1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 4, -1, 0, 4, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 3, -1, 0, 3, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 6, 1, 0, 6, 1>> const&, Eigen::Matrix<double, 1, -1, 1, 1, -1>::Scalar&, Eigen::Matrix<double, 1, -1, 1, 1, -1>::Scalar&, Eigen::PlainObjectBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>>&);
template void igl::cycodebase::point_cubic_squared_distance<Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 4, -1, 0, 4, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 4, -1, 0, 4, -1>> const&, Eigen::Matrix<double, 1, -1, 1, 1, -1>::Scalar&, Eigen::Matrix<double, 1, -1, 1, 1, -1>::Scalar&, Eigen::PlainObjectBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>>&);
template void igl::cycodebase::point_cubic_squared_distance<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1>>&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1>>&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>>&);
#endif
