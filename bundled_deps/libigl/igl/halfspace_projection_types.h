// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2026 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_HALFSPACE_PROJECTION_TYPES_H
#define IGL_HALFSPACE_PROJECTION_TYPES_H

#include "igl_inline.h"
#include <Eigen/Core>
#include <array>
#include <cstdint>

namespace igl
{
  /// Outcome of a fixed-dimension halfspace-intersection projection solve.
  enum class HalfspaceProjectionStatus
  {
    /// p is feasible and optimal to the requested tolerances.
    SUCCESS = 0,
    /// The halfspace intersection is empty (to the requested tolerances).
    INFEASIBLE = 1,
    /// The solve could not certify a result (e.g. rank/near-parallel
    /// degeneracy that could not be resolved robustly).
    NUMERICAL_FAILURE = 2,
    /// More planes were supplied than options.max_planes allows.
    CAPACITY_EXCEEDED = 3
  };

  /// Bit flags for HalfspaceProjectionResult::diagnostics.
  enum HalfspaceProjectionDiagnosticBits : uint32_t
  {
    IGL_HSP_DIAG_REDUNDANT_PLANE_SEEN = 1u << 0,
    IGL_HSP_DIAG_RANK_DROP            = 1u << 1,
    IGL_HSP_DIAG_FALLBACK_USED        = 1u << 2,
    IGL_HSP_DIAG_CONSERVATIVE_OFFSET_APPLIED = 1u << 3
  };

  /// Tolerances and knobs for project_to_halfspace_intersection().
  ///
  /// Four separate tolerances are kept distinct on purpose (see the design
  /// spec's "Numerical and containment policy" section): each protects a
  /// different failure mode and conflating them makes failures harder to
  /// diagnose.
  ///
  /// @tparam Scalar  e.g. double
  template <typename Scalar>
  struct HalfspaceProjectionOptions
  {
    /// Detects parallel/rank-deficient constraint normals in the reduced
    /// subspace (||alpha|| below this is treated as redundant-or-infeasible
    /// rather than a valid new active constraint).
    Scalar eps_rank = Scalar(1e-9);
    /// Slack allowed when accepting a point as feasible: a plane is
    /// considered satisfied if dot(a,p) >= b - eps_feasible.
    Scalar eps_feasible = Scalar(1e-9);
    /// Slack allowed when accepting a KKT multiplier as nonnegative.
    Scalar eps_kkt = Scalar(1e-9);
    /// Optional conservative outward offset added to every plane's right-hand
    /// side before solving (a_i^T p >= b_i + eps_outward), for production use
    /// where a tiny numerical infeasibility must not be produced. Zero
    /// disables this (the default).
    Scalar eps_outward = Scalar(0);
    /// Deterministic seed for the pseudorandom plane processing order.
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    /// Optional cap on the number of planes accepted; 0 means unbounded.
    /// Exceeding this yields CAPACITY_EXCEEDED before any solving is
    /// attempted.
    size_t max_planes = 0;
  };

  /// Result of project_to_halfspace_intersection().
  ///
  /// @tparam Scalar  e.g. double
  /// @tparam Dim     fixed ambient dimension (3 for the primary use case)
  template <typename Scalar, int Dim>
  struct HalfspaceProjectionResult
  {
    HalfspaceProjectionStatus status = HalfspaceProjectionStatus::NUMERICAL_FAILURE;
    /// Solution point (valid only when status == SUCCESS).
    Eigen::Matrix<Scalar,Dim,1> p = Eigen::Matrix<Scalar,Dim,1>::Zero();
    /// Row indices (into the caller's A/b) of the independent support set,
    /// left-packed; unused trailing entries are -1.
    std::array<int,Dim> active_ids;
    /// Number of valid entries in active_ids (0..Dim).
    int active_count = 0;
    /// max_i(b_i - dot(a_i,p)); <= eps_feasible on success.
    Scalar max_violation = Scalar(0);
    /// KKT multipliers for the active planes, parallel to active_ids.
    std::array<Scalar,Dim> multipliers;
    /// Bitwise-OR of HalfspaceProjectionDiagnosticBits.
    uint32_t diagnostics = 0;

    HalfspaceProjectionResult()
    {
      active_ids.fill(-1);
      multipliers.fill(Scalar(0));
    }
  };
}

#endif
