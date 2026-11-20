// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "progressive_hulls.h"
#include "progressive_hulls_cost_and_placement.h"
#include "decimate.h"
#include "decimate_trivial_callbacks.h"
#include "block_self_intersections.h"
#include "max_faces_stopping_condition.h"
IGL_INLINE bool igl::progressive_hulls(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const size_t max_m,
  Eigen::MatrixXd & U,
  Eigen::MatrixXi & G,
  Eigen::VectorXi & J)
{
  std::vector<decimate_pre_post_collapse_callbacks_decorator> decorators;
  return progressive_hulls(V,F,max_m,decorators,U,G,J);
}

IGL_INLINE bool igl::progressive_hulls(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const size_t max_m,
  const bool block_intersections,
  Eigen::MatrixXd & U,
  Eigen::MatrixXi & G,
  Eigen::VectorXi & J)
{
  std::vector<decimate_pre_post_collapse_callbacks_decorator> decorators;
  if(block_intersections)
  {
    decorators.push_back(igl::block_self_intersections());
  }
  return progressive_hulls(V,F,max_m,decorators,U,G,J);
}

IGL_INLINE bool igl::progressive_hulls(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const size_t max_m,
  const std::vector<decimate_pre_post_collapse_callbacks_decorator> & decorators,
  Eigen::MatrixXd & U,
  Eigen::MatrixXi & G,
  Eigen::VectorXi & J)
{
  int m = F.rows();
  Eigen::VectorXi I;
  decimate_pre_collapse_callback pre_collapse;
  decimate_post_collapse_callback post_collapse;
  decimate_trivial_callbacks(pre_collapse,post_collapse);
  // progressive_hulls operates directly on the (closed, manifold) input, so all
  // faces are "real": orig_m == #F and there are no faces at infinity.
  for(const auto & decorator : decorators)
  {
    decorator(V,F,m,pre_collapse,post_collapse);
  }
  return decimate(
    V,
    F,
    progressive_hulls_cost_and_placement,
    max_faces_stopping_condition(m,(const int)m,max_m),
    pre_collapse,
    post_collapse,
    U,
    G,
    J,
    I);
}
