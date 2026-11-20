// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2026 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "simplex_simplex_squared_distance.h"
#include <Eigen/Dense>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace igl
{
  namespace simplex_simplex_squared_distance_detail
  {
    // Compile-time size arithmetic that propagates Eigen::Dynamic, so that
    // every intermediate is fixed-size (and therefore stack-allocated) exactly
    // when the corresponding input size is known at compile time.
    constexpr int minus_one(const int n)
    {
      return n == Eigen::Dynamic ? Eigen::Dynamic : n-1;
    }
    constexpr int plus(const int a, const int b)
    {
      return (a == Eigen::Dynamic || b == Eigen::Dynamic) ? Eigen::Dynamic : a+b;
    }
    // Number of corners of a simplex we're willing to index with a bitmask.
    // The recursion is exponential in this number, so anything near the limit
    // is astronomically infeasible anyway.
    constexpr int max_corners = 62;
    // Faces are only memoized when 2^(n1+n2) bits fit comfortably.
    constexpr int max_memo_shift = 20;

    inline int pop_count(std::uint64_t m)
    {
      int c = 0;
      while(m) { m &= m-1; c++; }
      return c;
    }
    inline int first_bit(std::uint64_t m)
    {
      assert(m != 0);
      int i = 0;
      while(!(m&1ull)) { m >>= 1; i++; }
      return i;
    }

    template <typename _Scalar, int _N1, int _N2, int _Dim>
    struct State
    {
      typedef _Scalar Scalar;
      static constexpr int N1 = _N1;
      static constexpr int N2 = _N2;
      static constexpr int Dim = _Dim;
      Eigen::Matrix<Scalar,N1,Dim> A;
      Eigen::Matrix<Scalar,N2,Dim> B;
      // Barycentric coordinates of the best pair found so far, w.r.t. _all_ of
      // A and B (entries for corners off the current face are zero).
      Eigen::Matrix<Scalar,N1,1> best_w1;
      Eigen::Matrix<Scalar,N2,1> best_w2;
      Scalar best;
      int n1,n2,dim;
      // Bitset of (mask1,mask2) face pairs already visited, or nullptr if
      // memoization is disabled. `best` only ever decreases, so a face pair
      // that was pruned stays pruned and one that was explored can never
      // improve on a second visit: skipping repeats is exact.
      std::uint64_t * visited;
    };

    // Consider the face of A spanned by the corners in `mask1` against the face
    // of B spanned by the corners in `mask2`. M1,M2 are the number of corners
    // in each face when known at compile time (Eigen::Dynamic otherwise), which
    // lets the recursion unroll into fixed-size linear algebra.
    template <int M1, int M2, typename StateType>
    void helper(
      StateType & S,
      const std::uint64_t mask1,
      const std::uint64_t mask2)
    {
      typedef typename StateType::Scalar Scalar;
      constexpr int N1 = StateType::N1;
      constexpr int N2 = StateType::N2;
      constexpr int Dim = StateType::Dim;

      // Nothing can improve on an intersection.
      if(S.best <= Scalar(0)) { return; }
      if(S.visited)
      {
        const std::size_t key =
          ((std::size_t)mask1 << S.n2) | (std::size_t)mask2;
        const std::uint64_t bit = 1ull << (key & 63);
        if(S.visited[key>>6] & bit) { return; }
        S.visited[key>>6] |= bit;
      }

      const int m1 = M1 == Eigen::Dynamic ? pop_count(mask1) : M1;
      const int m2 = M2 == Eigen::Dynamic ? pop_count(mask2) : M2;
      const int f1 = first_bit(mask1);
      const int f2 = first_bit(mask2);

      // Point-point.
      if(m1 == 1 && m2 == 1)
      {
        const Scalar d = (S.A.row(f1) - S.B.row(f2)).squaredNorm();
        if(d < S.best)
        {
          S.best = d;
          S.best_w1.setZero();
          S.best_w2.setZero();
          S.best_w1(f1) = Scalar(1);
          S.best_w2(f2) = Scalar(1);
        }
        return;
      }

      if constexpr(!(M1 == 1 && M2 == 1))
      {
        // Cheap lower bound before the expensive solve below: the two faces are
        // at least as far apart as their axis-aligned bounding boxes.
        {
          Scalar lb = 0;
          for(int d = 0;d<S.dim;d++)
          {
            Scalar lo1 = S.A(f1,d), hi1 = lo1, lo2 = S.B(f2,d), hi2 = lo2;
            for(int i = f1+1;i<S.n1;i++)
            {
              if(mask1 & (1ull<<i))
              {
                lo1 = std::min(lo1,S.A(i,d));
                hi1 = std::max(hi1,S.A(i,d));
              }
            }
            for(int i = f2+1;i<S.n2;i++)
            {
              if(mask2 & (1ull<<i))
              {
                lo2 = std::min(lo2,S.B(i,d));
                hi2 = std::max(hi2,S.B(i,d));
              }
            }
            const Scalar gap = std::max(lo1-hi2,lo2-hi1);
            if(gap > Scalar(0)) { lb += gap*gap; }
          }
          if(!(lb < S.best)) { return; }
        }

        // Barycentric coordinates are considered valid if they're only this far
        // negative. Keep this tight: falsely rejecting a valid point is
        // harmless (the boundary recursion will find it anyway) whereas falsely
        // accepting an exterior point introduces error.
        const Scalar tol = Scalar(1e-12);

        // Parameterize the two affine hulls:
        //
        //   a = A(f1,:) + u'*EA
        //   b = B(f2,:) + v'*EB
        //
        // and find their closest pair by least squares. The complete orthogonal
        // decomposition provides the minimum-norm least-squares solution (i.e.,
        // the pseudo-inverse solution), so rank-deficient (degenerate or
        // parallel) configurations are handled gracefully.
        constexpr int MCols = plus(minus_one(M1),minus_one(M2));
        Eigen::Matrix<Scalar,Dim,MCols> M;
        M.resize(S.dim,(m1-1)+(m2-1));
        int col = 0;
        for(int i = f1+1;i<S.n1;i++)
        {
          if(mask1 & (1ull<<i))
          {
            M.col(col++) = (S.A.row(i) - S.A.row(f1)).transpose();
          }
        }
        for(int i = f2+1;i<S.n2;i++)
        {
          if(mask2 & (1ull<<i))
          {
            M.col(col++) = -(S.B.row(i) - S.B.row(f2)).transpose();
          }
        }
        const Eigen::Matrix<Scalar,Dim,1> rhs =
          (S.B.row(f2) - S.A.row(f1)).transpose();
        // With a single unknown the minimum-norm least-squares solution is just
        // a projection, so skip the decomposition. This is the most common node
        // shape (a corner against an edge, or an edge against an edge in the
        // codimension-one recursion).
        const auto project = [&M,&rhs]()->Scalar
        {
          const Scalar mm = M.col(0).squaredNorm();
          return mm > Scalar(0) ? M.col(0).dot(rhs)/mm : Scalar(0);
        };
        Eigen::Matrix<Scalar,MCols,1> x;
        if constexpr(MCols == 1)
        {
          x(0) = project();
        }else if constexpr(MCols == Eigen::Dynamic)
        {
          if((m1-1)+(m2-1) == 1)
          {
            x.setZero(1);
            x(0) = project();
          }else
          {
            x = M.completeOrthogonalDecomposition().solve(rhs);
          }
        }else
        {
          x = M.completeOrthogonalDecomposition().solve(rhs);
        }

        Eigen::Matrix<Scalar,N1,1> w1 = Eigen::Matrix<Scalar,N1,1>::Zero(S.n1);
        Eigen::Matrix<Scalar,N2,1> w2 = Eigen::Matrix<Scalar,N2,1>::Zero(S.n2);
        col = 0;
        Scalar s1 = 0;
        for(int i = f1+1;i<S.n1;i++)
        {
          if(mask1 & (1ull<<i)) { w1(i) = x(col); s1 += x(col); col++; }
        }
        w1(f1) = Scalar(1) - s1;
        Scalar s2 = 0;
        for(int i = f2+1;i<S.n2;i++)
        {
          if(mask2 & (1ull<<i)) { w2(i) = x(col); s2 += x(col); col++; }
        }
        w2(f2) = Scalar(1) - s2;

        // Entries of w1,w2 off the current face are zero, so they contribute
        // nothing to these products and trivially pass the tests below.
        const Scalar affine_sqrd =
          ((w1.transpose()*S.A) - (w2.transpose()*S.B)).squaredNorm();

        // Distance between the affine hulls is a lower bound for this entire
        // recursive problem.
        if(!(affine_sqrd < S.best)) { return; }

        // If the affine-hull closest pair lies in both faces, then it is also
        // the closest pair of the faces.
        if(w1.minCoeff() >= -tol && w2.minCoeff() >= -tol)
        {
          S.best = affine_sqrd;
          S.best_w1 = w1;
          S.best_w2 = w2;
          return;
        }

        // Otherwise a closest pair occurs on the boundary of at least one face.
        // Recurse over all codimension-one facets.
        if constexpr(M1 == Eigen::Dynamic || M1 > 1)
        {
          if(m1 > 1)
          {
            for(int i = f1;i<S.n1;i++)
            {
              if(mask1 & (1ull<<i))
              {
                helper<minus_one(M1),M2>(S,mask1 & ~(1ull<<i),mask2);
                if(S.best <= Scalar(0)) { return; }
              }
            }
          }
        }
        if constexpr(M2 == Eigen::Dynamic || M2 > 1)
        {
          if(m2 > 1)
          {
            for(int i = f2;i<S.n2;i++)
            {
              if(mask2 & (1ull<<i))
              {
                helper<M1,minus_one(M2)>(S,mask1,mask2 & ~(1ull<<i));
                if(S.best <= Scalar(0)) { return; }
              }
            }
          }
        }
      }
    }
  }
}

template
  <typename DerivedV1,
    typename DerivedV2,
    typename sqrdType,
    typename DerivedB1,
    typename DerivedB2>
IGL_INLINE void igl::simplex_simplex_squared_distance(
  const Eigen::MatrixBase<DerivedV1> & V1,
  const Eigen::MatrixBase<DerivedV2> & V2,
  sqrdType & sqrdDist,
  Eigen::MatrixBase<DerivedB1> & B1,
  Eigen::MatrixBase<DerivedB2> & B2)
{
  namespace detail = igl::simplex_simplex_squared_distance_detail;
  // assert that V1 and V2 have the same number of columns (static) or at least
  // one is dynamic and check that runtime have same columns (runtime)
  static_assert(
    DerivedV1::ColsAtCompileTime == Eigen::Dynamic ||
    DerivedV2::ColsAtCompileTime == Eigen::Dynamic ||
    (int)DerivedV1::ColsAtCompileTime == (int)DerivedV2::ColsAtCompileTime,
    "V1 and V2 must have the same number of columns");
  assert(V1.cols() == V2.cols() && "V1 and V2 must have the same dimension");
  assert(V1.rows() > 0 && "V1 must not be empty");
  assert(V2.rows() > 0 && "V2 must not be empty");
  assert(V1.rows() <= detail::max_corners && "V1 has too many corners");
  assert(V2.rows() <= detail::max_corners && "V2 has too many corners");
  // similarly assert that number of rows in V1 is the same as the number of
  // elements in B1 and that B1 is a vector
  static_assert(
    DerivedB1::RowsAtCompileTime == 1 || DerivedB1::ColsAtCompileTime == 1 ||
    DerivedB1::RowsAtCompileTime == Eigen::Dynamic ||
    DerivedB1::ColsAtCompileTime == Eigen::Dynamic,
    "B1 must be an Eigen vector");
  assert(
    (DerivedB1::SizeAtCompileTime == Eigen::Dynamic ||
     DerivedB1::SizeAtCompileTime == V1.rows()) &&
    "B1 must be dynamic or have V1.rows() entries");
  // ditto for V2 and B2
  static_assert(
    DerivedB2::RowsAtCompileTime == 1 || DerivedB2::ColsAtCompileTime == 1 ||
    DerivedB2::RowsAtCompileTime == Eigen::Dynamic ||
    DerivedB2::ColsAtCompileTime == Eigen::Dynamic,
    "B2 must be an Eigen vector");
  assert(
    (DerivedB2::SizeAtCompileTime == Eigen::Dynamic ||
     DerivedB2::SizeAtCompileTime == V2.rows()) &&
    "B2 must be dynamic or have V2.rows() entries");

  typedef typename std::common_type<
    typename DerivedV1::Scalar, typename DerivedV2::Scalar>::type Scalar;
  constexpr int N1 = DerivedV1::RowsAtCompileTime;
  constexpr int N2 = DerivedV2::RowsAtCompileTime;
  // Either input may pin down the shared dimension.
  constexpr int Dim = DerivedV1::ColsAtCompileTime != Eigen::Dynamic ?
    (int)DerivedV1::ColsAtCompileTime : (int)DerivedV2::ColsAtCompileTime;

  const int n1 = (int)V1.rows();
  const int n2 = (int)V2.rows();

  detail::State<Scalar,N1,N2,Dim> S;
  S.A = V1.template cast<Scalar>();
  S.B = V2.template cast<Scalar>();
  S.best = std::numeric_limits<Scalar>::infinity();
  S.best_w1 = Eigen::Matrix<Scalar,N1,1>::Zero(n1);
  S.best_w2 = Eigen::Matrix<Scalar,N2,1>::Zero(n2);
  S.n1 = n1;
  S.n2 = n2;
  S.dim = (int)V1.cols();

  // The face-pair memo lives on the stack whenever the simplex sizes are known
  // at compile time (e.g. 8 bytes for a triangle-triangle query).
  constexpr int NSum = (N1 == Eigen::Dynamic || N2 == Eigen::Dynamic) ?
    -1 : N1+N2;
  constexpr bool static_memo = NSum >= 0 && NSum <= detail::max_memo_shift;
  constexpr std::size_t static_words =
    static_memo ? (((std::size_t)1<<(NSum < 0 ? 0 : NSum))+63)/64 : 1;
  std::uint64_t static_buf[static_words] = {};
  std::vector<std::uint64_t> dynamic_buf;
  if(static_memo)
  {
    S.visited = static_buf;
  }else if(n1+n2 <= detail::max_memo_shift)
  {
    dynamic_buf.assign((((std::size_t)1<<(n1+n2))+63)/64,0);
    S.visited = dynamic_buf.data();
  }else
  {
    S.visited = nullptr;
  }

  // Seed with the closest pair of corners. This is already a valid answer, and
  // it gives the lower-bound tests inside the recursion something to prune
  // against from the very first node.
  for(int i = 0;i<n1;i++)
  {
    for(int j = 0;j<n2;j++)
    {
      const Scalar d = (S.A.row(i) - S.B.row(j)).squaredNorm();
      if(d < S.best)
      {
        S.best = d;
        S.best_w1.setZero();
        S.best_w2.setZero();
        S.best_w1(i) = Scalar(1);
        S.best_w2(j) = Scalar(1);
      }
    }
  }

  detail::helper<N1,N2>(
    S,
    (n1 == 64 ? (std::uint64_t)0 : ((std::uint64_t)1<<n1)) - 1,
    (n2 == 64 ? (std::uint64_t)0 : ((std::uint64_t)1<<n2)) - 1);

  sqrdDist = (sqrdType)S.best;
  // Resize outputs, respecting whether they're row- or column-vectors.
  if constexpr(DerivedB1::RowsAtCompileTime == 1)
  {
    B1.derived().resize(1,n1);
    B1.derived() = S.best_w1.transpose().template
      cast<typename DerivedB1::Scalar>();
  }else
  {
    B1.derived().resize(n1,1);
    B1.derived() = S.best_w1.template cast<typename DerivedB1::Scalar>();
  }
  if constexpr(DerivedB2::RowsAtCompileTime == 1)
  {
    B2.derived().resize(1,n2);
    B2.derived() = S.best_w2.transpose().template
      cast<typename DerivedB2::Scalar>();
  }else
  {
    B2.derived().resize(n2,1);
    B2.derived() = S.best_w2.template cast<typename DerivedB2::Scalar>();
  }
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::simplex_simplex_squared_distance<Eigen::Matrix<float, 2, 3, 0, 2, 3>, Eigen::Matrix<float, 3, 3, 0, 3, 3>, float, Eigen::Matrix<float, 2, 1, 0, 2, 1>, Eigen::Matrix<float, 3, 1, 0, 3, 1> >(Eigen::MatrixBase<Eigen::Matrix<float, 2, 3, 0, 2, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<float, 3, 3, 0, 3, 3> > const&, float&, Eigen::MatrixBase<Eigen::Matrix<float, 2, 1, 0, 2, 1> >&, Eigen::MatrixBase<Eigen::Matrix<float, 3, 1, 0, 3, 1> >&);
template void igl::simplex_simplex_squared_distance<Eigen::Matrix<double, 3, 3, 0, 3, 3>, Eigen::Matrix<double, 3, 3, 0, 3, 3>, double, Eigen::Matrix<double, 3, 1, 0, 3, 1>, Eigen::Matrix<double, 3, 1, 0, 3, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, 3, 3, 0, 3, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 3, 3, 0, 3, 3> > const&, double&, Eigen::MatrixBase<Eigen::Matrix<double, 3, 1, 0, 3, 1> >&, Eigen::MatrixBase<Eigen::Matrix<double, 3, 1, 0, 3, 1> >&);
template void igl::simplex_simplex_squared_distance<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, double, Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, double&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1> >&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1> >&);
template void igl::simplex_simplex_squared_distance<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, double, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, double&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&);
template void igl::simplex_simplex_squared_distance<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, double, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, double&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&);
template void igl::simplex_simplex_squared_distance<Eigen::Matrix<double, 4, 3, 0, 4, 3>, Eigen::Matrix<double, 4, 3, 0, 4, 3>, double, Eigen::Matrix<double, 4, 1, 0, 4, 1>, Eigen::Matrix<double, 4, 1, 0, 4, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, 4, 3, 0, 4, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<double, 4, 3, 0, 4, 3> > const&, double&, Eigen::MatrixBase<Eigen::Matrix<double, 4, 1, 0, 4, 1> >&, Eigen::MatrixBase<Eigen::Matrix<double, 4, 1, 0, 4, 1> >&);
#endif
