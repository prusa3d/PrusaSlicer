// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_SPHERE_MESH_WEDGE_H
#define IGL_SPHERE_MESH_WEDGE_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// A class to compute the signed distance to a "Sphere-Mesh Wedge" as seen in
  /// variable radius offset surfaces or Sphere-Meshes. Each wedge is defined
  /// by three vertices and three radii, one at each vertex. The wedge is
  /// the union of all spheres at points on the triangle with radius linearly
  /// interpolated. See, e.g., "Sphere-Meshes for Real-Time Hand Modeling and
  /// Tracking" or "A Multilinear Model for Bidirectional Craniofacial
  /// Reconstruction" or "Sphere-Meshes: Shape Approximation using Spherical
  /// Quadric Error Metrics" or "Variable-Radius Offset Surface Approximation on
  /// the GPU".
  ///
  template <typename Scalar>
  class SphereMeshWedge
  {
    public: 
      using RowVector3S = Eigen::Matrix<Scalar, 1, 3>;
      // Fields
      enum 
      {
        BIG_VERTEX = 0,
        BIG_EDGE = 1,
        NO_TRIANGLE = 2,
        FULL = 3
      } flavor;
      Eigen::Matrix<Scalar,3,3,Eigen::RowMajor> V;
      Eigen::Matrix<Scalar,3,1> r;
      Eigen::Matrix<Scalar,3,3,Eigen::RowMajor> EV;
      Eigen::Matrix<Scalar,3,1> l,l2,rr,a2,il2;
      int max_i;
      Eigen::Matrix<Scalar,5,4,Eigen::RowMajor> planes;
      Eigen::Matrix<Scalar,3,3,Eigen::RowMajor> T;
      Eigen::Matrix<Scalar,3,3,Eigen::RowMajor> C;

      SphereMeshWedge(){}
      /// Constructor that takes three vertices and three radii
      ///
      /// @param V0  first vertex position
      /// @param V1  second vertex position
      /// @param V2  third vertex position
      /// @param r0  radius at first vertex
      /// @param r1  radius at second vertex
      /// @param r2  radius at third vertex
      IGL_INLINE SphereMeshWedge(
        const RowVector3S & V0,
        const RowVector3S & V1,
        const RowVector3S & V2,
        const Scalar r0,
        const Scalar r1,
        const Scalar r2);
      /// @param[in] p  3-vector query point
      /// @return signed distance to the wedge at point p
      IGL_INLINE Scalar operator()(const RowVector3S & p) const;
    private:
      /// Precompute planes used for determining bounded signed to the skewed
      /// triangular slab portion.
      ///
      /// @return true if planes are well defined (false implies this slab has
      /// no contribution).
      IGL_INLINE bool compute_planes();
      /// Compute the signed distance to the wedge at a point p for the edge
      /// (i,j) 
      ///
      /// @param[in] p  3-vector query point
      /// @param[in] i  index of first vertex (0,1,2)
      /// @param[in] j  index of second vertex (0,1,2)
      IGL_INLINE Scalar round_cone_signed_distance(const RowVector3S & p, const int i, const int j) const;
  };
}

#ifndef IGL_STATIC_LIBRARY
#include "SphereMeshWedge.cpp"
#endif
#endif 
