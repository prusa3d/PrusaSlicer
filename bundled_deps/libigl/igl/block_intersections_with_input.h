// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BLOCK_INTERSECTIONS_WITH_INPUT_H
#define IGL_BLOCK_INTERSECTIONS_WITH_INPUT_H
#include "igl_inline.h"
#include "decimate_callback_types.h"
#include <Eigen/Core>
namespace igl
{
  /// Construct a decimation callback decorator that prevents edge collapses
  /// from making the mesh being decimated intersect a separate, _static_ mesh
  /// `(VB,FB)` (an obstacle or clearance shape).
  ///
  /// Because the obstacle is fixed, a single igl::AABB tree is built once and
  /// reused for every collapse test — no dynamic BVH maintenance is required.
  /// Only _transversal_ (non-coplanar) triangle-triangle intersections are
  /// blocked, so tangential flush contact against the obstacle is permitted
  /// while genuine piercing is not.
  ///
  /// \note Intended for a _disjoint_ obstacle (e.g. an offset / clearance
  /// shape). It may also be used against an obstacle that coincides with the
  /// mesh being decimated, but see igl::collapse_edge_would_intersect_mesh for
  /// the shared-vertex caveat there (a pierce very near a shared vertex may be
  /// missed); true self-intersection blocking should use
  /// igl::block_self_intersections instead.
  ///
  /// #### Example
  ///
  /// ```cpp
  /// std::vector<igl::decimate_pre_post_collapse_callbacks_decorator> decorators;
  /// decorators.push_back(igl::block_self_intersections());
  /// decorators.push_back(igl::block_intersections_with_input(VB,FB));
  /// igl::qslim(V,F,max_m,decorators,U,G,J,I);
  /// ```
  ///
  /// @param[in] VB  #VB by 3 list of static-mesh vertex positions
  /// @param[in] FB  #FB by 3 list of static-mesh face indices into VB
  /// @return decorator suitable for igl::decimate / igl::qslim
  ///
  /// \see decimate, qslim, block_self_intersections,
  ///   collapse_edge_would_intersect_mesh
  IGL_INLINE decimate_pre_post_collapse_callbacks_decorator
    block_intersections_with_input(
      const Eigen::MatrixXd & VB,
      const Eigen::MatrixXi & FB);
}
#ifndef IGL_STATIC_LIBRARY
#  include "block_intersections_with_input.cpp"
#endif
#endif
