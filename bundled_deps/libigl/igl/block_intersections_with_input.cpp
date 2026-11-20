// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "block_intersections_with_input.h"
#include "collapse_edge_would_intersect_mesh.h"
#include "AABB.h"
#include <memory>
#include <tuple>

IGL_INLINE igl::decimate_pre_post_collapse_callbacks_decorator
  igl::block_intersections_with_input(
    const Eigen::MatrixXd & VB,
    const Eigen::MatrixXi & FB)
{
  using Tree = igl::AABB<Eigen::MatrixXd,3>;
  // Build the static obstacle tree once and share ownership with the returned
  // callbacks (the obstacle never changes during decimation).
  auto tree = std::make_shared<Tree>();
  tree->init(VB,FB);
  auto VBp = std::make_shared<Eigen::MatrixXd>(VB);
  auto FBp = std::make_shared<Eigen::MatrixXi>(FB);

  return [tree,VBp,FBp](
    const Eigen::MatrixXd & /*V*/,
    const Eigen::MatrixXi & /*F*/,
    const int orig_m,
    igl::decimate_pre_collapse_callback  & pre_collapse,
    igl::decimate_post_collapse_callback & post_collapse)
  {
    // Only the pre_collapse callback needs wrapping: the obstacle is static, so
    // there is nothing to update after a successful collapse.
    igl::decimate_pre_collapse_callback inner_pre = pre_collapse;
    pre_collapse = [inner_pre,tree,VBp,FBp,orig_m](
      const Eigen::MatrixXd & V,
      const Eigen::MatrixXi & F,
      const Eigen::MatrixXi & E,
      const Eigen::VectorXi & EMAP,
      const Eigen::MatrixXi & EF,
      const Eigen::MatrixXi & EI,
      const igl::min_heap< std::tuple<double,int,int> > & Q,
      const Eigen::VectorXi & EQ,
      const Eigen::MatrixXd & C,
      const int e) -> bool
    {
      if(!inner_pre(V,F,E,EMAP,EF,EI,Q,EQ,C,e))
      {
        return false;
      }
      return !igl::collapse_edge_would_intersect_mesh(
        e, C.row(e).eval(), V,F,E,EMAP,EF,EI, *VBp,*FBp,*tree, orig_m);
    };
  };
}
