// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_LAZY_CAGE_H
#define IGL_LAZY_CAGE_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  /// Objective minimized when choosing the offset amount σ for igl::lazy_cage.
  enum LazyCageMetric
  {
    /// Smallest offset amount σ (tightest offset). Monotonic in σ, so the
    /// smallest _valid_ offset is found by bisection.
    LAZY_CAGE_METRIC_SIGMA = 0,
    /// Smallest volume enclosed by the (decimated) cage. Because decimation can
    /// remove more volume when given more room, the minimum-volume cage may
    /// occur at a larger σ than the minimum-σ cage, so σ is swept.
    LAZY_CAGE_METRIC_VOLUME = 1,
    /// Smallest surface area of the (decimated) cage. Swept like volume.
    LAZY_CAGE_METRIC_SURFACE_AREA = 2,
    NUM_LAZY_CAGE_METRIC = 3
  };
  /// How the dense offset isosurface is extracted for each candidate σ.
  enum LazyCageGridMode
  {
    /// Sample the signed distance field once on a dense `grid_size`³ background
    /// grid and re-run marching cubes at each σ isolevel. Amortizes the (costly)
    /// distance evaluation across all σ candidates; best for moderate grids.
    LAZY_CAGE_GRID_DENSE = 0,
    /// For each σ, prune to the octree cells near the offset surface with a
    /// Lipschitz octree and run sparse marching cubes there, evaluating the
    /// distance only at the O(surface-area) corners that matter. Loses the
    /// amortized dense precompute but avoids the O(grid³) cost, so it wins for
    /// large inputs and/or very dense grids. `grid_size` is rounded to the
    /// nearest power of two.
    LAZY_CAGE_GRID_SPARSE = 1,
    NUM_LAZY_CAGE_GRID_MODE = 2
  };
  /// Which distance field the offset isosurface is extracted from.
  enum LazyCageDistance
  {
    /// Signed distance (via fast winding number). The offset is the outward
    /// {distance = σ} isosurface. Requires a closed, consistently oriented
    /// input.
    LAZY_CAGE_DISTANCE_SIGNED = 0,
    /// Unsigned distance. The offset is the {distance = σ} isosurface, a closed
    /// shell wrapping the input at distance σ on all sides. Needs no orientation
    /// and works for inputs that are hard to orient, open, or genuinely a
    /// mid-surface of a thin sheet (e.g. cloth). Also avoids the winding-number
    /// evaluation entirely.
    LAZY_CAGE_DISTANCE_UNSIGNED = 1,
    NUM_LAZY_CAGE_DISTANCE = 2
  };
  /// Heuristic isosurfacing grid resolution (cells across the largest side) for
  /// a lazy cage with `num_faces` faces, used when `grid_size <= 0`. Roughly
  /// proportional to sqrt(num_faces) and clamped; see igl::lazy_cage.
  ///
  /// @param[in] num_faces  desired number of cage faces
  /// @return grid resolution
  IGL_INLINE int lazy_cage_default_grid_size(const int num_faces);
  /// Compute a "lazy cage" enclosing a closed input mesh: the coarse cage is
  /// obtained by offsetting (V,F) by an amount σ, extracting the dense offset
  /// surface, and decimating it down to `num_faces` faces while it remains a
  /// valid cage — enclosing the input, free of self-intersections, and not
  /// intersecting the input surface.
  ///
  /// The offset can be the signed-distance isosurface (default; requires an
  /// orientable closed input) or the unsigned-distance isosurface (see
  /// LazyCageDistance), which wraps open / non-orientable inputs such as the
  /// mid-surface of a thin sheet. Enclosure and non-intersection are verified
  /// against the (always closed, orientable) cage, so the input itself need not
  /// be orientable.
  ///
  /// The offset-and-decimate cage construction follows Sellán et al.,
  /// "Breaking Good: Fracture Modes for Realtime Destruction" (ACM TOG 2023).
  /// Unlike that formulation — which decimates without intersection awareness
  /// and repairs self-intersections afterward with a boolean self-union — this
  /// implementation blocks intersecting collapses during decimation (see
  /// igl::block_self_intersections, igl::block_intersections_with_input), so the
  /// output has exactly `num_faces` faces and needs no repair.
  ///
  /// Collapses that would cross the input or create self-intersections are
  /// refused during decimation (via igl::block_self_intersections and
  /// igl::block_intersections_with_input). A candidate offset σ succeeds only if
  /// the resulting mesh reaches `num_faces` AND is verified to enclose the input
  /// without intersecting it. Too small an offset either leaves no room to
  /// decimate to the target without hitting the input, or (when σ is below the
  /// isosurfacing grid resolution) is too poorly resolved to even separate from
  /// the input.
  ///
  /// The offset amount is chosen to minimize `metric` over valid cages:
  ///   - LAZY_CAGE_METRIC_SIGMA: the smallest valid σ, found by bisection.
  ///   - LAZY_CAGE_METRIC_VOLUME / _SURFACE_AREA: the cage volume/area (computed
  ///     by surface integral _after_ decimation) is not monotonic in σ — a
  ///     larger offset can give decimation enough room to remove more volume/area
  ///     than a tighter offset would — so σ is bisected to the valid floor and
  ///     then swept up to `max_sigma`, keeping the best.
  ///
  /// @param[in] V  #V by 3 list of input vertex positions (closed, manifold)
  /// @param[in] F  #F by 3 list of input triangle indices into V
  /// @param[in] num_faces  desired number of faces in the cage
  /// @param[in] grid_size  isosurfacing resolution: number of cells along the
  ///   largest bounding-box side used to sample the offset (larger = denser
  ///   offset, more faithful but slower; must be fine enough that the smallest
  ///   feasible offset can be resolved). If `<= 0`, a resolution is chosen from
  ///   `num_faces` via igl::lazy_cage_default_grid_size.
  /// @param[in] max_sigma  upper bound on the searched offset distance
  /// @param[in] num_iters  number of bisection iterations on σ (also the number
  ///   of sweep samples for the volume/area metrics)
  /// @param[in] use_qslim  if true decimate with igl::qslim (quadric error),
  ///   otherwise with the default {shortest edge, midpoint} igl::decimate
  /// @param[in] metric  objective minimized when choosing σ (see LazyCageMetric)
  /// @param[in] grid_mode  dense vs. sparse isosurface extraction (see
  ///   LazyCageGridMode)
  /// @param[in] distance  signed vs. unsigned distance field (see
  ///   LazyCageDistance)
  /// @param[out] CV  #CV by 3 list of cage vertex positions
  /// @param[out] CF  #CF by 3 list of cage triangle indices into CV
  /// @param[out] sigma  the offset distance used to build the returned cage
  /// @return true if a valid cage with `num_faces` faces was found within
  ///   `[0,max_sigma]`, false otherwise (in which case CV,CF hold the best valid
  ///   attempt, if any, and `sigma` its offset)
  ///
  /// \see decimate, qslim, block_self_intersections,
  ///   block_intersections_with_input, signed_distance, marching_cubes,
  ///   lipschitz_octree, centroid
  IGL_INLINE bool lazy_cage(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const int num_faces,
    const int grid_size,
    const double max_sigma,
    const int num_iters,
    const bool use_qslim,
    const LazyCageMetric metric,
    const LazyCageGridMode grid_mode,
    const LazyCageDistance distance,
    Eigen::MatrixXd & CV,
    Eigen::MatrixXi & CF,
    double & sigma);
  /// \overload
  /// \brief Uses max_sigma = 0.1*bbox-diagonal, num_iters = 12, the default
  /// {shortest edge, midpoint} decimation, the σ metric, the dense grid, and
  /// signed distance.
  /// If `grid_size <= 0` a resolution is chosen automatically from `num_faces`.
  IGL_INLINE bool lazy_cage(
    const Eigen::MatrixXd & V,
    const Eigen::MatrixXi & F,
    const int num_faces,
    const int grid_size,
    Eigen::MatrixXd & CV,
    Eigen::MatrixXi & CF,
    double & sigma);
}
#ifndef IGL_STATIC_LIBRARY
#  include "lazy_cage.cpp"
#endif
#endif
