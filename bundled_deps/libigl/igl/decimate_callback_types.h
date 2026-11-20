// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2020 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_DECIMATE_CALLBACK_TYPES_H
#define IGL_DECIMATE_CALLBACK_TYPES_H
#include <Eigen/Core>
#include "min_heap.h"
/// @file decimate_callback_types.h
///
/// See decimate.h for more details.
namespace igl
{
  /// Function handle used to control the cost of each edge collapse in
  /// igl::decimate.
  ///
  /// See decimate.h for more details.
  ///
  /// @param[in] e  index into E of edge to be collapsed
  /// @param[in] V  #V by dim list of vertex positions, lesser index of E(e,:) will be set
  ///     to midpoint of edge.
  /// @param[in] F  #F by 3 list of face indices into V.
  /// @param[in] E  #E by 2 list of edge indices into V.
  /// @param[in] EMAP #F*3 list of indices into E, mapping each directed edge to unique
  ///     unique edge in E
  /// @param[in] EF  #E by 2 list of edge flaps, EF(e,0)=f means e=(i-->j) is the edge of
  ///     F(f,:) opposite the vth corner, where EI(e,0)=v. Similarly EF(e,1) "
  ///     e=(j->i)
  /// @param[in] EI  #E by 2 list of edge flap corners (see above).
  /// @param[out] cost  cost of collapsing edge e
  /// @param[out] p  placement of merged vertex resulting from collapse
  using decimate_cost_and_placement_callback = 
    std::function<void(
      const int                                           ,/*e*/
      const Eigen::MatrixXd &                             ,/*V*/
      const Eigen::MatrixXi &                             ,/*F*/
      const Eigen::MatrixXi &                             ,/*E*/
      const Eigen::VectorXi &                             ,/*EMAP*/
      const Eigen::MatrixXi &                             ,/*EF*/
      const Eigen::MatrixXi &                             ,/*EI*/
      double &                                            ,/*cost*/
      Eigen::RowVectorXd &                                 /*p*/
      )>;
  /// Function handle used to control whether the queue processing in 
  /// igl::decimate should stop.
  ///
  /// See decimate.h for more details.
  ///
  /// @param[in] V  #V by dim list of vertex positions, lesser index of E(e,:) will be set
  ///     to midpoint of edge.
  /// @param[in] F  #F by 3 list of face indices into V.
  /// @param[in] E  #E by 2 list of edge indices into V.
  /// @param[in] EMAP #F*3 list of indices into E, mapping each directed edge to unique
  ///     unique edge in E
  /// @param[in] EF  #E by 2 list of edge flaps, EF(e,0)=f means e=(i-->j) is the edge of
  ///     F(f,:) opposite the vth corner, where EI(e,0)=v. Similarly EF(e,1) "
  ///     e=(j->i)
  /// @param[in] EI  #E by 2 list of edge flap corners (see above).
  /// @param[in] Q  queue containing pairs of costs and edge indices and insertion "time"
  /// @param[in] EQ  #E list of "time" of last time pushed into Q
  /// @param[in] C  #E by dim list of stored placements
  /// @param[in] e  index into E of attempted collapsed edge. Set to -1 if Q is empty or
  ///               contains only infinite cost edges.
  /// @param[in] e1  index into E of edge collapsed on left.
  /// @param[in] e2  index into E of edge collapsed on right.
  /// @param[in] f1  index into F of face collapsed on left.
  /// @param[in] f2  index into F of face collapsed on right.
  /// @return whether to stop
  using decimate_stopping_condition_callback = 
    std::function<bool(
      const Eigen::MatrixXd &                             ,/*V*/
      const Eigen::MatrixXi &                             ,/*F*/
      const Eigen::MatrixXi &                             ,/*E*/
      const Eigen::VectorXi &                             ,/*EMAP*/
      const Eigen::MatrixXi &                             ,/*EF*/
      const Eigen::MatrixXi &                             ,/*EI*/
      const igl::min_heap< std::tuple<double,int,int> > & ,/*Q*/
      const Eigen::VectorXi &                             ,/*EQ*/
      const Eigen::MatrixXd &                             ,/*C*/
      const int                                           ,/*e*/
      const int                                           ,/*e1*/
      const int                                           ,/*e2*/
      const int                                           ,/*f1*/
      const int                                            /*f2*/
      )>;
  /// Function handle called just before `collapse_edge` is attempted. If this
  /// function returns false then the collapse is aborted. 
  ///
  /// See decimate.h for more details.
  ///
  /// @param[in] V  #V by dim list of vertex positions, lesser index of E(e,:) will be set
  ///     to midpoint of edge.
  /// @param[in] F  #F by 3 list of face indices into V.
  /// @param[in] E  #E by 2 list of edge indices into V.
  /// @param[in] EMAP #F*3 list of indices into E, mapping each directed edge to unique
  ///     unique edge in E
  /// @param[in] EF  #E by 2 list of edge flaps, EF(e,0)=f means e=(i-->j) is the edge of
  ///     F(f,:) opposite the vth corner, where EI(e,0)=v. Similarly EF(e,1)
  ///     e=(j->i)
  /// @param[in] EI  #E by 2 list of edge flap corners (see above).
  /// @param[in] Q  queue containing pairs of costs and edge indices and insertion "time"
  /// @param[in] EQ  #E list of "time" of last time pushed into Q
  /// @param[in] C  #E by dim list of stored placements
  /// @param[in] e  index into E of attempted collapsed edge. Set to -1 if Q is empty or
  ///               contains only infinite cost edges.
  /// @return true if collapse should be carried out
  using decimate_pre_collapse_callback = 
    std::function<bool(
      const Eigen::MatrixXd &                             ,/*V*/
      const Eigen::MatrixXi &                             ,/*F*/
      const Eigen::MatrixXi &                             ,/*E*/
      const Eigen::VectorXi &                             ,/*EMAP*/
      const Eigen::MatrixXi &                             ,/*EF*/
      const Eigen::MatrixXi &                             ,/*EI*/
      const igl::min_heap< std::tuple<double,int,int> > & ,/*Q*/
      const Eigen::VectorXi &                             ,/*EQ*/
      const Eigen::MatrixXd &                             ,/*C*/
      const int                                            /*e*/
      )>;
  /// Function handle called just after `collapse_edge` is attempted. 
  ///
  /// See decimate.h for more details.
  ///
  /// @param[in] V  #V by dim list of vertex positions, lesser index of E(e,:) will be set
  ///     to midpoint of edge.
  /// @param[in] F  #F by 3 list of face indices into V.
  /// @param[in] E  #E by 2 list of edge indices into V.
  /// @param[in] EMAP #F*3 list of indices into E, mapping each directed edge to unique
  ///     unique edge in E
  /// @param[in] EF  #E by 2 list of edge flaps, EF(e,0)=f means e=(i-->j) is the edge of
  ///     F(f,:) opposite the vth corner, where EI(e,0)=v. Similarly EF(e,1)
  ///     e=(j->i)
  /// @param[in] EI  #E by 2 list of edge flap corners (see above).
  /// @param[in] Q  queue containing pairs of costs and edge indices and insertion "time"
  /// @param[in] EQ  #E list of "time" of last time pushed into Q
  /// @param[in] C  #E by dim list of stored placements
  /// @param[in] e  index into E of attempted collapsed edge. Set to -1 if Q is empty or
  ///               contains only infinite cost edges.
  /// @param[in] e1  index into E of edge collapsed on left.
  /// @param[in] e2  index into E of edge collapsed on right.
  /// @param[in] f1  index into F of face collapsed on left.
  /// @param[in] f2  index into F of face collapsed on right.
  /// @param[in] collapsed whether collapse actual took place
  using decimate_post_collapse_callback = 
    std::function<void(
      const Eigen::MatrixXd &                             ,/*V*/
      const Eigen::MatrixXi &                             ,/*F*/
      const Eigen::MatrixXi &                             ,/*E*/
      const Eigen::VectorXi &                             ,/*EMAP*/
      const Eigen::MatrixXi &                             ,/*EF*/
      const Eigen::MatrixXi &                             ,/*EI*/
      const igl::min_heap< std::tuple<double,int,int> > & ,/*Q*/
      const Eigen::VectorXi &                             ,/*EQ*/
      const Eigen::MatrixXd &                             ,/*C*/
      const int                                           ,/*e*/
      const int                                           ,/*e1*/
      const int                                           ,/*e2*/
      const int                                           ,/*f1*/
      const int                                           ,/*f2*/
      const bool                                           /*collapsed*/
      )>;
  /// Function handle used to "decorate" (wrap) the `pre_collapse` and
  /// `post_collapse` callbacks used by igl::decimate/igl::qslim. Given the
  /// current callbacks (in/out), a decorator replaces them with new callbacks
  /// that typically call through to the originals while adding behavior (e.g.,
  /// blocking collapses that would create intersections). Multiple decorators
  /// can be cascaded so that each wraps the result of the previous.
  ///
  /// The decorator is provided the _working_ mesh — that is, the input mesh
  /// after `connect_boundary_to_infinity` has been called — so that it may build
  /// any acceleration structures (e.g., an igl::AABB tree) whose leaves
  /// correspond to face indices in the working mesh. Faces with index `< orig_m`
  /// are "real" faces; faces with index `>= orig_m` are the fictitious faces
  /// incident on the point at infinity and should typically be ignored.
  ///
  /// A decorator is expected to own (e.g., via captured `std::shared_ptr`) any
  /// state it needs for the lifetime of the returned callbacks.
  ///
  /// @param[in] V  #V by dim list of working-mesh vertex positions
  /// @param[in] F  #F by 3 list of working-mesh face indices into V
  /// @param[in] orig_m  number of "real" faces: faces `[0,orig_m)` are real,
  ///   faces `[orig_m,#F)` are incident on the point at infinity
  /// @param[in,out] pre_collapse  pre_collapse callback to wrap
  /// @param[in,out] post_collapse  post_collapse callback to wrap
  ///
  /// \see block_self_intersections, block_intersections_with_input
  using decimate_pre_post_collapse_callbacks_decorator =
    std::function<void(
      const Eigen::MatrixXd &                             ,/*V*/
      const Eigen::MatrixXi &                             ,/*F*/
      const int                                           ,/*orig_m*/
      decimate_pre_collapse_callback  &                   ,/*pre_collapse*/
      decimate_post_collapse_callback &                    /*post_collapse*/
      )>;
}
#endif
