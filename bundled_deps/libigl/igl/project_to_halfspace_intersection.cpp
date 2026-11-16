#include "project_to_halfspace_intersection.h"
#include <Eigen/Dense>
#include <vector>
#include <array>
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>

namespace igl
{
  namespace internal
  {
    // Lightweight, deterministic PRNG for the plane-processing permutation.
    // std::mt19937_64 carries ~2.5kB of state and its seeding step alone
    // costs more than the entire rest of a small solve here; splitmix64 (Vigna)
    // is a few instructions per call and statistically adequate for a
    // Fisher-Yates shuffle of a handful of elements -- the shuffle only needs
    // to break adversarial plane orderings, not pass PRNG test suites.
    struct SplitMix64
    {
      uint64_t state;
      explicit SplitMix64(uint64_t seed) : state(seed) {}
      IGL_INLINE uint64_t operator()()
      {
        uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
      }
    };

    // Fisher-Yates shuffle of ids[0:n) using SplitMix64. Avoids
    // std::uniform_int_distribution's rejection-sampling overhead; a
    // Lemire-style bounded reduction is more than sufficient for this use.
    IGL_INLINE void small_shuffle(int * ids, int n, uint64_t seed)
    {
      SplitMix64 rng(seed);
      for(int i = n - 1;i > 0;i--)
      {
        const int j = static_cast<int>(rng() % static_cast<uint64_t>(i + 1));
        std::swap(ids[i], ids[j]);
      }
    }
    // Orthonormal basis of the orthogonal complement of a unit vector in
    // R^D, D a compile-time constant. A struct template (not a function
    // template) so it can be partially specialized per-D while keeping
    // Scalar generic. D==1,2,3 use the design spec's originally-suggested
    // explicit closed-form routines (null_basis_2_to_1, a hand cross-product
    // construction for 3-to-2) rather than a general QR: at these sizes the
    // arithmetic is a handful of flops with zero matrix-factorization
    // overhead, which measurably matters when this runs per-collapse-candidate
    // in a mesh simplifier. D>3 falls back to Householder QR (not expected
    // on the primary Dim=3 path, kept only so the template stays generic).
    template <typename Scalar, int D>
    struct OrthonormalComplement
    {
      static IGL_INLINE Eigen::Matrix<Scalar,D,D-1> compute(
        const Eigen::Matrix<Scalar,D,1> & unit_vec)
      {
        Eigen::HouseholderQR<Eigen::Matrix<Scalar,D,1>> qr(unit_vec);
        const Eigen::Matrix<Scalar,D,D> Q = qr.householderQ();
        return Q.template rightCols<D-1>();
      }
    };

    // D=1 -> 0: no free directions remain; nothing to compute.
    template <typename Scalar>
    struct OrthonormalComplement<Scalar,1>
    {
      static IGL_INLINE Eigen::Matrix<Scalar,1,0> compute(const Eigen::Matrix<Scalar,1,1> &)
      {
        return Eigen::Matrix<Scalar,1,0>();
      }
    };

    // D=2 -> 1: a 90-degree rotation of a unit 2-vector is already unit and
    // orthogonal -- no factorization needed at all.
    template <typename Scalar>
    struct OrthonormalComplement<Scalar,2>
    {
      static IGL_INLINE Eigen::Matrix<Scalar,2,1> compute(const Eigen::Matrix<Scalar,2,1> & n)
      {
        return Eigen::Matrix<Scalar,2,1>(-n.y(), n.x());
      }
    };

    // D=3 -> 2: branchless orthonormal-basis construction, robust for any
    // unit n (including n close to (0,0,-1)). Duff, Burgess, Christensen,
    // Hery, Kensler, Liani, Villemin, "Building an Orthonormal Basis,
    // Revisited" (JCGT 2017) -- this is the standard fast/robust technique
    // and satisfies the spec's "robust fallback axis selection" requirement
    // without any axis case-analysis.
    template <typename Scalar>
    struct OrthonormalComplement<Scalar,3>
    {
      static IGL_INLINE Eigen::Matrix<Scalar,3,2> compute(const Eigen::Matrix<Scalar,3,1> & n)
      {
        const Scalar sign = n.z() >= Scalar(0) ? Scalar(1) : Scalar(-1);
        const Scalar a = Scalar(-1) / (sign + n.z());
        const Scalar b = n.x() * n.y() * a;
        Eigen::Matrix<Scalar,3,2> N;
        N.col(0) << Scalar(1) + sign * n.x() * n.x() * a, sign * b, -sign * n.x();
        N.col(1) << b, sign + n.y() * n.y() * a, -n.y();
        return N;
      }
    };

    // Recursive Seidel/SDLP-style projection of q onto the intersection of
    // the first `count` halfspaces named by `ids` (a fixed permutation
    // shared by every recursion level -- only the prefix *length* shrinks on
    // dimension reduction, so it's passed as a range rather than copied),
    // restricted to the affine subspace {o + U*z : z in R^D}. See
    // project_to_halfspace_intersection.h and
    // progressive_hulls_fixed_dim_qp_design.md, "Core algorithm".
    //
    // D (the current free dimension, Dim down to 0) is a *template*
    // parameter, not a runtime int: since Dim is small and known at compile
    // time, every U/alpha/N in the whole recursion is a fixed-size Eigen
    // type (stack-allocated, no Eigen::Dynamic), which is what actually
    // makes this fast at these problem sizes -- letting `d` be a runtime
    // value forced every intermediate matrix through Eigen's Dynamic path
    // (heap-backed storage, no compile-time unrolling) even though the
    // *size* was always known by construction to be <= Dim. The active set
    // (bounded by Dim by construction) lives in a fixed std::array, not a
    // std::vector, for the same reason.
    template <typename Scalar, int Dim, int D>
    struct HalfspaceRecursion
    {
      static IGL_INLINE bool solve(
        const Eigen::Matrix<Scalar,Dim,1> & q,
        const Eigen::Matrix<Scalar,Eigen::Dynamic,Dim> & A,
        const Eigen::Matrix<Scalar,Eigen::Dynamic,1> & b,
        const HalfspaceProjectionOptions<Scalar> & options,
        const int * ids,
        const int count,
        const Eigen::Matrix<Scalar,Dim,1> & o,
        const Eigen::Matrix<Scalar,Dim,D> & U,
        uint32_t & diagnostics,
        Eigen::Matrix<Scalar,Dim,1> & p_out,
        std::array<int,Dim> & active_out,
        int & active_count)
      {
        p_out = o + U * (U.transpose() * (q - o));
        active_count = 0;

        for(int idx = 0;idx < count;idx++)
        {
          const int i = ids[idx];
          const Scalar lhs = A.row(i) * p_out;
          if(lhs >= b(i) - options.eps_feasible) continue; // not active

          const Eigen::Matrix<Scalar,D,1> alpha = U.transpose() * A.row(i).transpose();
          const Scalar beta = b(i) - static_cast<Scalar>(A.row(i) * o);
          const Scalar alpha_sq = alpha.squaredNorm();
          const Scalar alpha_norm = std::sqrt(alpha_sq);

          if(alpha_norm <= options.eps_rank)
          {
            // a_i is (numerically) orthogonal to every remaining free
            // direction, so a_i^T p is constant (== a_i^T o) throughout this
            // subspace: either already-satisfied-elsewhere-but-flagged-here
            // (redundant, tolerate) or genuinely unsatisfiable here (infeasible).
            const Scalar lhs_o = A.row(i) * o;
            if(lhs_o >= b(i) - options.eps_feasible)
            {
              diagnostics |= IGL_HSP_DIAG_REDUNDANT_PLANE_SEEN;
              continue;
            }
            diagnostics |= IGL_HSP_DIAG_RANK_DROP;
            return false;
          }

          const Eigen::Matrix<Scalar,D,1> z0 = (beta / alpha_sq) * alpha;
          const Eigen::Matrix<Scalar,Dim,1> o_new = o + U * z0;
          const Eigen::Matrix<Scalar,D,1> alpha_unit = alpha / alpha_norm;
          const Eigen::Matrix<Scalar,D,D-1> N = OrthonormalComplement<Scalar,D>::compute(alpha_unit);
          const Eigen::Matrix<Scalar,Dim,D-1> U_new = U * N;

          Eigen::Matrix<Scalar,Dim,1> p2;
          std::array<int,Dim> active2;
          int active2_count = 0;
          if(!HalfspaceRecursion<Scalar,Dim,D-1>::solve(
               q, A, b, options, ids, idx, o_new, U_new, diagnostics, p2, active2, active2_count))
          {
            return false;
          }
          p_out = p2;
          active_out = active2;
          active_count = active2_count;
          active_out[active_count++] = i;
        }
        return true;
      }
    };

    // Base case: no free directions left. o is the unique point on the
    // active-so-far affine subspace; verify every processed plane holds
    // there. U is a Dim x 0 matrix (unused) -- kept only so every level of
    // the recursion shares one call signature.
    template <typename Scalar, int Dim>
    struct HalfspaceRecursion<Scalar, Dim, 0>
    {
      static IGL_INLINE bool solve(
        const Eigen::Matrix<Scalar,Dim,1> & /*q*/,
        const Eigen::Matrix<Scalar,Eigen::Dynamic,Dim> & A,
        const Eigen::Matrix<Scalar,Eigen::Dynamic,1> & b,
        const HalfspaceProjectionOptions<Scalar> & options,
        const int * ids,
        const int count,
        const Eigen::Matrix<Scalar,Dim,1> & o,
        const Eigen::Matrix<Scalar,Dim,0> & /*U, unused*/,
        uint32_t & /*diagnostics*/,
        Eigen::Matrix<Scalar,Dim,1> & p_out,
        std::array<int,Dim> & /*active_out*/,
        int & active_count)
      {
        for(int idx = 0;idx < count;idx++)
        {
          const int id = ids[idx];
          const Scalar lhs = A.row(id) * o;
          if(lhs < b(id) - options.eps_feasible) return false;
        }
        p_out = o;
        active_count = 0;
        return true;
      }
    };
  }
}

template <typename Scalar, int Dim>
IGL_INLINE igl::HalfspaceProjectionStatus igl::project_to_halfspace_intersection(
  const Eigen::Matrix<Scalar,Dim,1> & q,
  const Eigen::Matrix<Scalar,Eigen::Dynamic,Dim> & A,
  const Eigen::Matrix<Scalar,Eigen::Dynamic,1> & b,
  const igl::HalfspaceProjectionOptions<Scalar> & options,
  igl::HalfspaceProjectionResult<Scalar,Dim> & result)
{
  result = igl::HalfspaceProjectionResult<Scalar,Dim>();
  const Eigen::Index m = A.rows();

  if(!q.allFinite() || (m > 0 && (!A.allFinite() || !b.allFinite())))
  {
    result.status = igl::HalfspaceProjectionStatus::NUMERICAL_FAILURE;
    return result.status;
  }
  if(options.max_planes != 0 && static_cast<size_t>(m) > options.max_planes)
  {
    result.status = igl::HalfspaceProjectionStatus::CAPACITY_EXCEEDED;
    return result.status;
  }

  // Skip the b+eps_outward allocation entirely in the (common) eps_outward==0
  // case rather than always materializing a fresh Dynamic vector.
  Eigen::Matrix<Scalar,Eigen::Dynamic,1> b_eff_storage;
  const Eigen::Matrix<Scalar,Eigen::Dynamic,1> * b_eff_ptr = &b;
  if(options.eps_outward != Scalar(0))
  {
    b_eff_storage = b.array() + options.eps_outward;
    b_eff_ptr = &b_eff_storage;
  }
  const Eigen::Matrix<Scalar,Eigen::Dynamic,1> & b_eff = *b_eff_ptr;

  // Typical mesh-collapse vertex-ring valence fits comfortably in a stack
  // buffer; only fall back to the heap above that (still-correct) cap.
  constexpr int kStackCapacity = 64;
  int ids_stack[kStackCapacity];
  std::vector<int> ids_heap;
  int * ids_ptr = ids_stack;
  if(m > kStackCapacity)
  {
    ids_heap.resize(static_cast<size_t>(m));
    ids_ptr = ids_heap.data();
  }
  for(int i = 0;i < m;i++) ids_ptr[i] = i;
  igl::internal::small_shuffle(ids_ptr, static_cast<int>(m), options.seed);

  const Eigen::Matrix<Scalar,Dim,1> o = Eigen::Matrix<Scalar,Dim,1>::Zero();
  const Eigen::Matrix<Scalar,Dim,Dim> U = Eigen::Matrix<Scalar,Dim,Dim>::Identity();

  uint32_t diagnostics = 0;
  Eigen::Matrix<Scalar,Dim,1> p;
  std::array<int,Dim> active;
  int active_count_raw = 0;
  const bool feasible = igl::internal::HalfspaceRecursion<Scalar,Dim,Dim>::solve(
    q, A, b_eff, options, ids_ptr, static_cast<int>(m), o, U, diagnostics, p, active, active_count_raw);

  if(options.eps_outward > 0) diagnostics |= IGL_HSP_DIAG_CONSERVATIVE_OFFSET_APPLIED;
  result.diagnostics = diagnostics;

  if(!feasible)
  {
    result.status = igl::HalfspaceProjectionStatus::INFEASIBLE;
    return result.status;
  }
  if(!p.allFinite())
  {
    result.status = igl::HalfspaceProjectionStatus::NUMERICAL_FAILURE;
    return result.status;
  }

  result.p = p;
  result.active_count = std::min(active_count_raw, Dim);
  for(int r = 0;r < result.active_count;r++) result.active_ids[r] = active[static_cast<size_t>(r)];

  Scalar max_violation = Scalar(0);
  for(Eigen::Index i = 0;i < m;i++)
  {
    const Scalar v = b(i) - static_cast<Scalar>(A.row(i) * p);
    if(v > max_violation) max_violation = v;
  }
  result.max_violation = max_violation;

  if(result.active_count > 0)
  {
    const int k = result.active_count;
    Eigen::Matrix<Scalar,Eigen::Dynamic,Dim> A_S(k,Dim);
    Eigen::Matrix<Scalar,Eigen::Dynamic,1> b_S(k);
    for(int r = 0;r < k;r++)
    {
      A_S.row(r) = A.row(result.active_ids[r]);
      b_S(r) = b_eff(result.active_ids[r]);
    }
    const Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic> gram = A_S * A_S.transpose();
    const Eigen::Matrix<Scalar,Eigen::Dynamic,1> mu = gram.ldlt().solve(b_S - A_S * q);
    for(int r = 0;r < k;r++) result.multipliers[r] = mu(r);

    for(int r = 0;r < k;r++)
    {
      if(result.multipliers[r] < -options.eps_kkt)
      {
        result.status = igl::HalfspaceProjectionStatus::NUMERICAL_FAILURE;
        return result.status;
      }
    }
  }

  result.status = igl::HalfspaceProjectionStatus::SUCCESS;
  return result.status;
}

#ifdef IGL_STATIC_LIBRARY
template igl::HalfspaceProjectionStatus igl::project_to_halfspace_intersection<double,3>(
  const Eigen::Matrix<double,3,1> &,
  const Eigen::Matrix<double,Eigen::Dynamic,3> &,
  const Eigen::Matrix<double,Eigen::Dynamic,1> &,
  const igl::HalfspaceProjectionOptions<double> &,
  igl::HalfspaceProjectionResult<double,3> &);
#endif
