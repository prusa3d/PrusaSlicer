// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2026 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PROJECT_TO_HALFSPACE_INTERSECTION_H
#define IGL_PROJECT_TO_HALFSPACE_INTERSECTION_H

#include "igl_inline.h"
#include "halfspace_projection_types.h"
#include <Eigen/Core>

namespace igl
{
  /// Compute the Euclidean projection of a point onto the intersection of a
  /// fixed-dimensional set of halfspaces:
  ///
  ///   p* = argmin_p ||p-q||^2   s.t.   A p >= b   (row i of A/b is a_i^T p >= b_i)
  ///
  /// via a randomized-incremental (Seidel/SDLP-style) recursive solver
  /// specialized to a small fixed dimension `Dim` (primarily 3). This is the
  /// primitive behind the regularized progressive-hulls placement QP
  /// `min g^T p + (lambda/2)||p-p0||^2 s.t. A p >= b`, which reduces to this
  /// projection via `q = p0 - g/(2*lambda)`.
  ///
  /// Unlike a general-purpose active-set QP solver, this exploits that the
  /// active support has at most `Dim` independent planes and needs no
  /// dynamic allocation of scratch beyond per-call temporaries sized by the
  /// current recursion depth (<= Dim).
  ///
  /// @param[in] q  preferred (unconstrained) point
  /// @param[in] A  #A by Dim matrix; row i is the halfspace normal a_i
  /// @param[in] b  #A vector; row i is the halfspace offset b_i
  /// @param[in] options  tolerances and solver knobs (see HalfspaceProjectionOptions)
  /// @param[out] result  solution, active set, diagnostics (see HalfspaceProjectionResult)
  /// @return result.status
  template <typename Scalar, int Dim>
  IGL_INLINE HalfspaceProjectionStatus project_to_halfspace_intersection(
    const Eigen::Matrix<Scalar,Dim,1> & q,
    const Eigen::Matrix<Scalar,Eigen::Dynamic,Dim> & A,
    const Eigen::Matrix<Scalar,Eigen::Dynamic,1> & b,
    const HalfspaceProjectionOptions<Scalar> & options,
    HalfspaceProjectionResult<Scalar,Dim> & result);
}

#ifndef IGL_STATIC_LIBRARY
#  include "project_to_halfspace_intersection.cpp"
#endif

#endif
