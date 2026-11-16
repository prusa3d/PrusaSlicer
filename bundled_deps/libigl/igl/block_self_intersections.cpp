// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "block_self_intersections.h"
#include "intersection_blocking_collapse_edge_callbacks.h"
#include "AABB.h"
#include <Eigen/Core>
#include <memory>
#include <tuple>

IGL_INLINE igl::decimate_pre_post_collapse_callbacks_decorator
  igl::block_self_intersections()
{
  return [](
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const int orig_m,
    igl::decimate_pre_collapse_callback  & pre_collapse,
    igl::decimate_post_collapse_callback & post_collapse)
  {
    using Tree = igl::AABB<Eigen::MatrixXd,3>;
    // The tree's leaves must correspond to the _real_ faces of the working mesh
    // (faces [0,orig_m)). The fictitious faces incident on the point at
    // infinity are intentionally excluded; they carry face indices >= orig_m
    // and are skipped by the intersection blocking callbacks.
    //
    // The root of the tree changes as leaves are detached/refit during
    // decimation, so intersection_blocking_collapse_edge_callbacks captures the
    // tree pointer by reference. We stash that pointer in a shared holder with a
    // custom deleter so that the whole tree is freed exactly when the last copy
    // of the wrapped callbacks is destroyed.
    std::shared_ptr<Tree*> holder(
      new Tree*(new Tree()),
      [](Tree ** p)
      {
        if(p)
        {
          if(*p){ delete (*p)->root(); }
          delete p;
        }
      });
    (*holder)->init(
      Eigen::MatrixXd(V), Eigen::MatrixXi(F.topRows(orig_m)));

    igl::intersection_blocking_collapse_edge_callbacks(
      pre_collapse, post_collapse, // copied as needed
      *holder,
      pre_collapse, post_collapse);

    // Extend the lifetime of the tree to match that of the callbacks (in case
    // the decorator itself is destroyed before decimation finishes).
    igl::decimate_pre_collapse_callback  inner_pre  = pre_collapse;
    igl::decimate_post_collapse_callback inner_post = post_collapse;
    pre_collapse = [inner_pre,holder](
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
      return inner_pre(V,F,E,EMAP,EF,EI,Q,EQ,C,e);
    };
    post_collapse = [inner_post,holder](
      const Eigen::MatrixXd & V,
      const Eigen::MatrixXi & F,
      const Eigen::MatrixXi & E,
      const Eigen::VectorXi & EMAP,
      const Eigen::MatrixXi & EF,
      const Eigen::MatrixXi & EI,
      const igl::min_heap< std::tuple<double,int,int> > & Q,
      const Eigen::VectorXi & EQ,
      const Eigen::MatrixXd & C,
      const int e,
      const int e1,
      const int e2,
      const int f1,
      const int f2,
      const bool collapsed)
    {
      inner_post(V,F,E,EMAP,EF,EI,Q,EQ,C,e,e1,e2,f1,f2,collapsed);
    };
  };
}
