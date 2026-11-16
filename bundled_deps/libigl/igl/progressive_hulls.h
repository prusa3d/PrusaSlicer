// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_PROGRESSIVE_HULLS_H
#define IGL_PROGRESSIVE_HULLS_H
#include "igl_inline.h"
#include "decimate_callback_types.h"
#include <Eigen/Core>
#include <vector>

namespace igl
{
  /// Collapses edges until desired number of faces is achieved but ensures
  /// that new vertices are placed outside all previous meshes as per
  /// "progressive hulls" in "Silhouette clipping" [Sander et al. 2000].
  ///
  /// \pre Assumes (V,F) is a closed manifold mesh
  ///
  /// @param[in] V  #V by dim list of vertex positions
  /// @param[in] F  #F by 3 list of face indices into V.
  /// @param[in] max_m  desired number of output faces
  /// @param[out] U  #U by dim list of output vertex posistions (can be same ref as V)
  /// @param[out] G  #G by 3 list of output face indices into U (can be same ref as G)
  /// @param[out] J  #G list of indices into F of birth faces
  /// @return true if m was reached (otherwise #G > m)
  IGL_INLINE bool progressive_hulls(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const size_t max_m,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J);

  /// \overload
  ///
  /// \brief Progressive hulls, optionally blocking edge collapses that would
  /// introduce self-intersections (see igl::block_self_intersections). This is
  /// the convenience form matching igl::decimate / igl::qslim; it is equivalent
  /// to passing `{igl::block_self_intersections()}` as the decorators.
  ///
  /// @param[in] block_intersections  whether to block self-intersections
  IGL_INLINE bool progressive_hulls(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const size_t max_m,
    const bool block_intersections,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J);

  /// \overload
  ///
  /// \brief Progressive hulls with a list of user-supplied pre/post-collapse
  /// callback decorators (e.g., igl::block_self_intersections,
  /// igl::block_intersections_with_input, or a custom decorator) cascaded in
  /// order. Because progressive hulls place every new vertex outside the
  /// current mesh (the output contains the input), combining with
  /// igl::block_intersections_with_input against an outward offset yields a
  /// coarse enclosing hull guaranteed to stay within a clearance shell.
  ///
  /// @param[in] decorators  list of decorators to cascade (may be empty)
  ///
  /// \see igl::decimate, igl::block_self_intersections,
  ///   igl::block_intersections_with_input
  IGL_INLINE bool progressive_hulls(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const size_t max_m,
    const std::vector<decimate_pre_post_collapse_callbacks_decorator> & decorators,
    Eigen::MatrixXd & U,
    Eigen::MatrixXi & G,
    Eigen::VectorXi & J);
}
#ifndef IGL_STATIC_LIBRARY
#  include "progressive_hulls.cpp"
#endif
#endif
