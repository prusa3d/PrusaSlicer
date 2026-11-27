// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2026 Philip Trettner <trettner@shapedcode.com>, Cedric Martens <cedric.martens@umontreal.ca>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_WINDINGNUMBERANTIPODALSCENE_H
#define IGL_WINDINGNUMBERANTIPODALSCENE_H

#include "PI.h"
#include "parallel_for.h"

#include <Eigen/Core>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace igl
{
  /// Precomputed scene for the Antipodal Method generalized winding number
  ///
  /// The scene stores only the weighted open-boundary edges of the input
  /// triangle mesh and a fixed antipodal reference direction `x0`. Closed
  /// (manifold) meshes have an empty boundary and the fractional term is
  /// exactly zero.
  ///
  /// Querying the winding number additionally requires an `Intersector` that
  /// returns the signed ray-mesh crossing count along `(p, x0)` over the
  /// original mesh; the scene itself is intersector-agnostic. See
  /// `igl::embree::EmbreeIntersector::signedIntersectionsRay` for an
  /// optimized concrete implementation.
  ///
  /// ### Intersector concept
  /// A type `I` satisfies the concept when it exposes:
  ///   - `using OriginType    = ...;`     (3D row vector type)
  ///   - `using DirectionType = ...;`     (3D row vector type)
  ///   - `int signedIntersectionsRay(
  ///         OriginType origin, DirectionType direction,
  ///         /* defaulted tnear, tfar, mask */) const;`
  ///
  /// `winding_number` casts query point/direction to the intersector's types
  /// at the call site, so a `double`-precision scene against a float-only
  /// intersector works without an adaptor.
  template <typename Scalar>
  class WindingNumberAntipodalScene
  {
  public:
    using Point     = Eigen::Matrix<Scalar, 1, 3>;
    using Direction = Eigen::Matrix<Scalar, 1, 3>;

  private:
    struct WeightedSeg
    {
      Point a;
      Point b;
      Scalar w;
    };

  public:
    /// Build a scene from a 3D triangle mesh.
    ///
    /// @param[in] V   #V by 3 list of vertex positions
    /// @param[in] F   #F by 3 list of triangle indices
    /// @param[in] x0  unit reference direction (defaults to a fixed non-axis-aligned vector)
    template <typename DerivedV, typename DerivedF>
    WindingNumberAntipodalScene(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Direction & x0 = default_x0())
      : m_x0(x0), m_face_count(static_cast<size_t>(F.rows()))
    {
      assert(V.cols() == 3 && "WindingNumberAntipodalScene: only 3D vertex positions are supported");
      assert(F.cols() == 3 && "WindingNumberAntipodalScene: only triangle meshes are supported");
      build_boundary_segments(V, F, m_boundary);
    }

    /// Single-point query: full generalized winding number at `p`
    /// (fractional + signed integer crossings).
    template <typename Intersector, typename Derivedp>
    Scalar winding_number(
      const Intersector & intersector,
      const Eigen::MatrixBase<Derivedp> & p) const
    {
      const Point pp(static_cast<Scalar>(p(0)),
                     static_cast<Scalar>(p(1)),
                     static_cast<Scalar>(p(2)));
      const Direction x1 = -m_x0;
      Scalar area = Scalar(0);
      for (const auto & ws : m_boundary)
      {
        const Point v0 = ws.a - pp;
        const Point v1 = ws.b - pp;
        area += ws.w * half_solid_angle_unorm(x1, v0, v1);
      }
      const Scalar frac = area / (Scalar(2) * Scalar(igl::PI));

      using IO = typename Intersector::OriginType;
      using ID = typename Intersector::DirectionType;
      const IO io(static_cast<typename IO::Scalar>(pp(0)),
                  static_cast<typename IO::Scalar>(pp(1)),
                  static_cast<typename IO::Scalar>(pp(2)));
      const ID id(static_cast<typename ID::Scalar>(m_x0(0)),
                  static_cast<typename ID::Scalar>(m_x0(1)),
                  static_cast<typename ID::Scalar>(m_x0(2)));
      const int c = intersector.signedIntersectionsRay(io, id);

      return frac + Scalar(c);
    }

    /// Batch query, parallelized via `igl::parallel_for`.
    ///
    /// @param[in]  intersector  Concept-compatible intersector built over the
    ///                          same mesh used to construct the scene. Must
    ///                          be safe to query concurrently.
    /// @param[in]  O            #O by 3 list of query points
    /// @param[out] W            #O by 1 list of winding numbers
    template <typename Intersector, typename DerivedO, typename DerivedW>
    void winding_number(
      const Intersector & intersector,
      const Eigen::MatrixBase<DerivedO> & O,
      Eigen::PlainObjectBase<DerivedW> & W) const
    {
      W.resize(O.rows(), 1);

      // Adaptive parallel-for threshold.
      //
      // The libigl thread pool has a roughly fixed ~1 ms TOTAL overhead per
      // parallel_for invocation (not per iteration). So we only spawn the
      // pool when the WHOLE batch is expected to take ≥ 1 ms.
      //
      // Per-query work heuristic:
      //   t_q ≈ 50 * B + 100 * sqrt(F)  ns
      // with B = boundary segment count, F = triangle count. Pool wins once
      //   t_q * O > 10^6 ns  ⇒  O > 10^6 / t_q
      // which is exactly parallel_for's `min_parallel` semantics.
      //
      // (This is a rough heuristic and should be revisited once parallel_for becomes lower-overhead)
      const double t_q_ns =
          50.0  * static_cast<double>(m_boundary.size()) +
          100.0 * std::sqrt(static_cast<double>(m_face_count));
      const size_t min_parallel = static_cast<size_t>(
          std::ceil(1.0e6 / std::max(t_q_ns, 1.0)));

      igl::parallel_for(O.rows(), [&](const int o)
      {
        W(o) = winding_number(intersector, O.row(o));
      }, min_parallel);
    }

    /// Reference direction `x0` used to evaluate this scene.
    const Direction & x0() const { return m_x0; }
    /// Number of weighted boundary edge segments.
    size_t num_boundary_segments() const { return m_boundary.size(); }
    /// Number of triangles in the original mesh.
    size_t num_faces() const { return m_face_count; }

    /// Default reference direction: normalize(1, sqrt(2), sqrt(3)). A fixed
    /// non-axis-aligned unit vector; any unit vector works per the paper;
    /// this choice avoids accidental alignment with axis-aligned geometry.
    static Direction default_x0()
    {
      Direction d(Scalar(1),
                  Scalar(std::sqrt(2.0)),
                  Scalar(std::sqrt(3.0)));
      return d / d.norm();
    }

    /// Half the signed solid angle subtended by the spherical triangle
    /// (x1, v0, v1) at the origin, via the unnormalized
    /// Van Oosterom-Strackee formula. `x1` is expected to be a unit vector
    /// (the antipodal "south pole" `-x0`); `v0`, `v1` need not be normalized.
    /// Callers accumulate per-edge contributions and divide by 2π.
    static Scalar half_solid_angle_unorm(
      const Direction & x1, const Point & v0, const Point & v1)
    {
      const Scalar l0 = v0.norm();
      const Scalar l1 = v1.norm();
      const Scalar num   = x1.dot(v0.cross(v1));
      const Scalar denom = l0 * l1
                         + l1 * x1.dot(v0)
                         + l0 * x1.dot(v1)
                         + v0.dot(v1);
      return std::atan2(num, denom);
    }

    /// Extract the open boundary as oriented, weighted edge segments.
    ///
    /// Each undirected edge {min, max} accumulates +1 for every triangle
    /// that traverses it as min→max and -1 for every traversal max→min.
    /// Interior edges of an oriented manifold cancel to zero and are
    /// dropped. Surviving edges are emitted with positive weight; the
    /// segment direction is flipped when the net count is negative so the
    /// weight is always > 0. Non-manifold (≥3 incident triangles per edge)
    /// is handled the same way. The surviving net count becomes the weight.
    template <typename DerivedV, typename DerivedF>
    static void build_boundary_segments(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      std::vector<WeightedSeg> & out)
    {
      out.clear();
      std::unordered_map<std::uint64_t, int> counts;
      counts.reserve(static_cast<size_t>(F.rows()) * 3);

      const auto pack = [](int lo, int hi) -> std::uint64_t
      {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lo)) << 32)
             |  static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi));
      };

      for (Eigen::Index t = 0; t < F.rows(); ++t)
      {
        const int tri[3] = {
          static_cast<int>(F(t, 0)),
          static_cast<int>(F(t, 1)),
          static_cast<int>(F(t, 2))
        };
        for (int e = 0; e < 3; ++e)
        {
          const int u = tri[e];
          const int v = tri[(e + 1) % 3];
          const int lo = u < v ? u : v;
          const int hi = u < v ? v : u;
          const std::uint64_t k = pack(lo, hi);
          counts[k] += (u < v) ? +1 : -1;
        }
      }

      out.reserve(counts.size());
      for (const auto & kv : counts)
      {
        const int sum = kv.second;
        if (sum == 0) continue;
        const std::uint32_t lo = static_cast<std::uint32_t>(kv.first >> 32);
        const std::uint32_t hi = static_cast<std::uint32_t>(kv.first & 0xFFFFFFFFu);
        const Point a(static_cast<Scalar>(V(lo, 0)),
                      static_cast<Scalar>(V(lo, 1)),
                      static_cast<Scalar>(V(lo, 2)));
        const Point b(static_cast<Scalar>(V(hi, 0)),
                      static_cast<Scalar>(V(hi, 1)),
                      static_cast<Scalar>(V(hi, 2)));
        WeightedSeg ws;
        if (sum > 0) { ws.a = a; ws.b = b; ws.w = static_cast<Scalar>(sum); }
        else         { ws.a = b; ws.b = a; ws.w = static_cast<Scalar>(-sum); }
        out.push_back(ws);
      }
    }

    std::vector<WeightedSeg> m_boundary;
    Direction m_x0;
    size_t m_face_count = 0;
  };
}

#endif
