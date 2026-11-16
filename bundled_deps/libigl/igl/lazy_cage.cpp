// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "lazy_cage.h"
#include "signed_distance.h"
#include "point_mesh_squared_distance.h"
#include "voxel_grid.h"
#include "marching_cubes.h"
#include "decimate.h"
#include "qslim.h"
#include "block_self_intersections.h"
#include "block_intersections_with_input.h"
#include "bounding_box_diagonal.h"
#include "centroid.h"
#include "doublearea.h"
#include "AABB.h"
#include "find_intersections.h"
#include "fast_winding_number.h"
#include "lipschitz_octree.h"
#include "unique_sparse_voxel_corners.h"
#include "parallel_for.h"
#include <Eigen/Geometry>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace
{
  // Objective value of a valid cage (smaller is better) under `metric`.
  double cage_metric(
    const Eigen::MatrixXd & CV,
    const Eigen::MatrixXi & CF,
    const double s,
    const igl::LazyCageMetric metric)
  {
    switch(metric)
    {
      case igl::LAZY_CAGE_METRIC_VOLUME:
      {
        Eigen::RowVector3d c; double vol = 0;
        igl::centroid(CV,CF,c,vol);
        return std::abs(vol);
      }
      case igl::LAZY_CAGE_METRIC_SURFACE_AREA:
      {
        Eigen::VectorXd dblA;
        igl::doublearea(CV,CF,dblA);
        return 0.5*dblA.sum();
      }
      case igl::LAZY_CAGE_METRIC_SIGMA:
      default:
        return s;
    }
  }
}

IGL_INLINE int igl::lazy_cage_default_grid_size(const int num_faces)
{
  // A cage with N faces has a characteristic edge length ~ 1/sqrt(N) of its
  // extent, so the isosurfacing grid needs on the order of sqrt(N) cells across
  // to resolve the offset that decimation targets. Beyond that, finer grids buy
  // little tightness for a lot of cost (they can even force a *looser* offset,
  // since a more faithful offset leaves the coarse cage less room). The constant
  // and clamps below are fit to a mesh/parameter sweep (see 1007_LazyCage).
  const double g = 3.5*std::sqrt((double)std::max(1,num_faces));
  int gi = (int)std::lround(g);
  if(gi < 24) { gi = 24; }
  if(gi > 160) { gi = 160; }
  return gi;
}

IGL_INLINE bool igl::lazy_cage(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const int num_faces,
  const int grid_size_in,
  const double max_sigma,
  const int num_iters,
  const bool use_qslim,
  const LazyCageMetric metric,
  const LazyCageGridMode grid_mode,
  const LazyCageDistance distance,
  Eigen::MatrixXd & CV,
  Eigen::MatrixXi & CF,
  double & sigma)
{
  const double diag = igl::bounding_box_diagonal(V);
  const bool unsigned_distance = (distance == LAZY_CAGE_DISTANCE_UNSIGNED);
  const int grid_size = grid_size_in > 0 ?
    grid_size_in : lazy_cage_default_grid_size(num_faces);

  // Tree over the input, reused to validate every candidate cage (and, in
  // sparse mode, to evaluate distances for the signed distance function).
  igl::AABB<Eigen::MatrixXd,3> input_tree;
  input_tree.init(V,F);

  // === Backend-specific precompute + an extract(σ) -> (OV,OF) closure. ===
  // DENSE: sample the signed distance field once on a grid_size³ grid; each
  //   candidate offset is a different marching cubes isolevel (amortized).
  Eigen::MatrixXd GV;
  Eigen::RowVector3i side;
  Eigen::VectorXd Sdense;
  // SPARSE: point-wise signed distance handle + a padded octree root; each
  //   candidate offset prunes to near-surface cells and runs sparse marching
  //   cubes there (no amortization, but O(surface) instead of O(grid³)).
  igl::FastWindingNumberBVH fwn;
  std::function<double(const Eigen::RowVector3d &)> sdf;   // signed (needs winding)
  std::function<double(const Eigen::RowVector3d &)> udist; // unsigned (distance only)
  Eigen::RowVector3d origin;
  double h0 = 0;
  int max_depth = 0;

  // The scalar field whose σ-isosurface is the offset: signed distance (outward
  // offset, needs orientation) or unsigned distance (a shell wrapping the input
  // on all sides; needs no orientation).
  if(grid_mode == LAZY_CAGE_GRID_DENSE)
  {
    igl::voxel_grid(V, max_sigma*1.5, grid_size, 1, GV, side);
    if(unsigned_distance)
    {
      Eigen::VectorXd sqrD; Eigen::VectorXi I; Eigen::MatrixXd C;
      igl::point_mesh_squared_distance(GV, V, F, sqrD, I, C);
      Sdense = sqrD.array().sqrt();
    }
    else
    {
      Eigen::VectorXi I; Eigen::MatrixXd C,N;
      igl::signed_distance(
        GV, V, F, igl::SIGNED_DISTANCE_TYPE_FAST_WINDING_NUMBER,
        -std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::infinity(), Sdense, I, C, N);
    }
  }
  else
  {
    const igl::AABB<Eigen::MatrixXd,3> & tree = input_tree;
    udist = [&V,&F,&tree](const Eigen::RowVector3d & p) -> double
    {
      int i; Eigen::RowVector3d c;
      return std::sqrt(tree.squared_distance(V,F,p,i,c));
    };
    if(unsigned_distance)
    {
      // The offset field is the unsigned distance itself — no winding number.
      sdf = udist;
    }
    else
    {
      igl::fast_winding_number(V.cast<float>().eval(), F, 2, fwn);
      sdf = [&V,&F,&fwn,&udist](const Eigen::RowVector3d & p) -> double
      {
        const double w = igl::fast_winding_number(fwn, 2, p.cast<float>().eval());
        return (1.0 - 2.0*std::abs(w))*udist(p);   // > 0 outside, < 0 inside
      };
    }
    const Eigen::RowVector3d bmin = V.colwise().minCoeff();
    const Eigen::RowVector3d bmax = V.colwise().maxCoeff();
    const double maxside = (bmax-bmin).maxCoeff();
    h0 = maxside + 3.0*max_sigma;                 // room for offsets up to max_sigma
    // Octree resolution is a power of two; pick the nearest to grid_size so the
    // sparse mode is a fair drop-in for the dense mode at the same grid_size.
    max_depth = std::max(1,(int)std::lround(std::log2((double)grid_size)));
    origin = 0.5*(bmin+bmax) - Eigen::RowVector3d::Constant(0.5*h0);
  }

  const auto extract_offset = [&](
    const double s, Eigen::MatrixXd & OV, Eigen::MatrixXi & OF)
  {
    if(grid_mode == LAZY_CAGE_GRID_DENSE)
    {
      igl::marching_cubes(Sdense, GV, side(0), side(1), side(2), s, OV, OF);
      return;
    }
    // Sparse: prune to octree cells straddling the offset surface {sdf = s}.
    // Pruning only needs a 1-Lipschitz lower bound on the distance to that
    // surface, so use the *unsigned* distance (no winding number): |udist - s|.
    // This also flags an inner shell at unsigned distance s, but sdf < 0 there
    // so marching cubes finds no surface — safe over-inclusion that keeps the
    // expensive winding-number evaluation off the many pruning queries.
    const std::function<double(const Eigen::RowVector3d &)> udf =
      [&udist,s](const Eigen::RowVector3d & p){ return std::abs(udist(p)-s); };
    Eigen::Matrix<int,Eigen::Dynamic,3,Eigen::RowMajor> ijk;
    igl::lipschitz_octree(origin, h0, max_depth, udf, ijk);
    if(ijk.rows() == 0) { OV.resize(0,3); OF.resize(0,3); return; }
    Eigen::Matrix<int,Eigen::Dynamic,3,Eigen::RowMajor> unique_ijk;
    Eigen::Matrix<int,Eigen::Dynamic,8,Eigen::RowMajor> J;
    Eigen::Matrix<double,Eigen::Dynamic,3,Eigen::RowMajor> corners;
    igl::unique_sparse_voxel_corners(
      origin, h0, max_depth, ijk, unique_ijk, J, corners);
    Eigen::VectorXd Sc(corners.rows());
    igl::parallel_for(
      corners.rows(),
      [&](const int u){ Sc(u) = sdf(corners.row(u)); }, 1000);
    // Use the fixed-3-column output types that the sparse marching_cubes is
    // instantiated for, then assign into the caller's OV/OF.
    Eigen::Matrix<double,Eigen::Dynamic,3> mV;
    Eigen::Matrix<int,Eigen::Dynamic,3> mF;
    igl::marching_cubes(Sc, corners, J, s, mV, mF);
    OV = mV;
    OF = mF;
  };

  // The obstacle (the input) is static across all candidate offsets, so build
  // this decorator — and its AABB tree over the input — exactly once and reuse
  // it for every decimation. (block_self_intersections must be re-created per
  // candidate because its tree tracks the changing offset mesh.)
  const igl::decimate_pre_post_collapse_callbacks_decorator dec_input =
    igl::block_intersections_with_input(V,F);

  // Attempt to build a valid cage at a given offset: extract the offset
  // isosurface, decimate it to `num_faces` while forbidding self-intersections
  // and intersections with the input, then verify the result is actually a
  // valid cage (reached the target, does not intersect the input, and encloses
  // it). Merely reaching the target is not sufficient: if the offset is under
  // the grid resolution the isosurface itself may already cross the input, and
  // the intersection-blocking callbacks only prevent *new* intersections.
  const auto try_sigma = [&](
    const double s, Eigen::MatrixXd & U, Eigen::MatrixXi & G) -> bool
  {
    Eigen::MatrixXd OV;
    Eigen::MatrixXi OF;
    extract_offset(s, OV, OF);
    if(OF.rows() <= num_faces) { return false; }
    std::vector<igl::decimate_pre_post_collapse_callbacks_decorator> decorators;
    decorators.push_back(igl::block_self_intersections());
    decorators.push_back(dec_input);
    Eigen::VectorXi J,I;
    const bool reached = use_qslim ?
      igl::qslim   (OV,OF,num_faces,decorators,U,G,J,I) :
      igl::decimate(OV,OF,num_faces,decorators,U,G,J,I);
    if(!reached || G.rows() == 0) { return false; }
    // The cage must not transversally (non-coplanar) pierce the input; a
    // coplanar flush contact is not a crossing. Reuse the input tree.
    Eigen::MatrixXi IF; Eigen::Array<bool,Eigen::Dynamic,1> CP;
    igl::find_intersections(input_tree,V,F,U,G,/*first_only=*/false,IF,CP);
    for(int i = 0;i<CP.size();i++){ if(!CP(i)) { return false; } }
    // ...and it must enclose the input: since the cage does not intersect the
    // input, the (connected) input is wholly inside or wholly outside; require
    // every input vertex to be strictly inside the cage.
    Eigen::VectorXd Sd; Eigen::VectorXi Id; Eigen::MatrixXd Cd,Nd;
    igl::signed_distance(
      V,U,G,igl::SIGNED_DISTANCE_TYPE_FAST_WINDING_NUMBER,
      -std::numeric_limits<double>::infinity(),
       std::numeric_limits<double>::infinity(),Sd,Id,Cd,Nd);
    if((Sd.array() > 1e-7*diag).count() > 0) { return false; }
    return true;
  };

  // The upper bound must succeed; otherwise there is no valid cage in range.
  double hi = max_sigma;
  Eigen::MatrixXd U_hi;
  Eigen::MatrixXi G_hi;
  const bool hi_ok = try_sigma(hi, U_hi, G_hi);
  CV = U_hi; CF = G_hi; sigma = hi;
  if(!hi_ok) { return false; }

  // Bisection for the smallest σ that still yields a valid cage. `lo` is kept as
  // a known (assumed) failure and is never evaluated at 0 (where the offset
  // coincides with the input). This is the answer for the σ metric and the
  // lower end of the sweep for the volume/area metrics.
  double lo = 0.0;
  for(int it = 0; it < num_iters; it++)
  {
    const double mid = 0.5*(lo+hi);
    Eigen::MatrixXd U;
    Eigen::MatrixXi G;
    if(try_sigma(mid, U, G))
    {
      hi = mid;
      CV = U; CF = G; sigma = mid;
    }else
    {
      lo = mid;
    }
  }
  if(metric == LAZY_CAGE_METRIC_SIGMA) { return true; }

  // Volume / surface area are not monotonic in σ: giving decimation more room
  // (a larger offset) can remove more volume/area than the tightest offset
  // allows. Sweep σ from the valid floor up to max_sigma and keep the best.
  double best = cage_metric(CV,CF,sigma,metric);
  const double floor_sigma = sigma;
  for(int i = 1; i <= num_iters; i++)
  {
    const double s = floor_sigma + (max_sigma-floor_sigma)*(double(i)/num_iters);
    Eigen::MatrixXd U;
    Eigen::MatrixXi G;
    if(try_sigma(s, U, G))
    {
      const double m = cage_metric(U,G,s,metric);
      if(m < best)
      {
        best = m;
        CV = U; CF = G; sigma = s;
      }
    }
  }
  return true;
}

IGL_INLINE bool igl::lazy_cage(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const int num_faces,
  const int grid_size,
  Eigen::MatrixXd & CV,
  Eigen::MatrixXi & CF,
  double & sigma)
{
  const double diag = igl::bounding_box_diagonal(V);
  return lazy_cage(
    V,F,num_faces,grid_size,0.1*diag,12,false,
    LAZY_CAGE_METRIC_SIGMA,LAZY_CAGE_GRID_DENSE,LAZY_CAGE_DISTANCE_SIGNED,
    CV,CF,sigma);
}
