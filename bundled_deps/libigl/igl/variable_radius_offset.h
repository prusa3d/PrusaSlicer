// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_VARIABLE_RADIUS_OFFSET_H
#define IGL_VARIABLE_RADIUS_OFFSET_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
#include <functional>

namespace igl
{
  template <typename Scalar>
  class SphereMeshWedge;

  /// Compute the variable radius offset (see, e.g., "Variable-Radius Offset
  /// Curves and Surfaces" [Lin & Rokne 1997]) of a triangle mesh with a
  /// pieceiwse linear radius function.
  ///
  /// @param[in] V  #V by 3 list of vertex positions
  /// @param[in] F  #F by 3 list of triangle indices into V
  /// @param[in] R  #V list of radii for each vertex
  /// @param[in] origin  3-vector origin of octree used for mesh extraction
  /// @param[in] h0  side length of octree root cell
  /// @param[in] max_depth  maximum depth of octree used for mesh extraction
  ///   (root is depth=0)
  /// @param[out] mV #mV by 3 list of vertex positions of the offset mesh
  /// @param[out] mF #mF by 3 list of triangle indices into mV
  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedR,
    typename Derivedorigin,
    typename DerivedmV,
    typename DerivedmF>
  void variable_radius_offset(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedR> & R,
      const Eigen::MatrixBase<Derivedorigin> & origin,
      const typename Derivedorigin::Scalar h0,
      const int max_depth,
      Eigen::PlainObjectBase<DerivedmV> & mV,
      Eigen::PlainObjectBase<DerivedmF> & mF);

  /// \brief Overload which prepares data for a eytzinger_aabb_sdf callable sdf
  /// function.
  ///
  /// @param[in] V  #V by 3 list of vertex positions
  /// @param[in] F  #F by 3 list of triangle indices into V
  /// @param[in] R  #V list of radii for each vertex
  /// @param[out] PB1 #F by 3 list of minimum corners of each offset
  ///   triangle's axis aligned bounding box
  /// @param[out] PB2 #F by 3 list of maximum corners of each offset
  ///   triangle's axis aligned bounding box
  /// @param[out] #F list of precomputed sphere-mesh wedge data
  /// @param[out] primitive  function handle that takes as input a point and
  ///   primitive index as input and outputs the corresponding signed distance.
  ///   This function assumes that `data` will remain alive.
  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedR,
    typename DerivedPB1,
    typename DerivedPB2,
    typename Scalar>
  void variable_radius_offset(
      const Eigen::MatrixBase<DerivedV> & V,
      const Eigen::MatrixBase<DerivedF> & F,
      const Eigen::MatrixBase<DerivedR> & R,
      Eigen::PlainObjectBase<DerivedPB1> & PB1,
      Eigen::PlainObjectBase<DerivedPB2> & PB2,
      std::vector<igl::SphereMeshWedge<Scalar>> & data,
      std::function<Scalar(const Eigen::Matrix<Scalar,1,3> &,const int i)> & 
        primitive);

  ///// \brief Overload using precomputed data.
  template <
    typename DerivedPB1,
    typename DerivedPB2,
    typename Scalar,
    typename Derivedorigin,
    typename DerivedmV,
    typename DerivedmF>
  void variable_radius_offset(
    const Eigen::MatrixBase<DerivedPB1> & PB1,
    const Eigen::MatrixBase<DerivedPB2> & PB2,
    const std::vector<igl::SphereMeshWedge<Scalar>> & data,
    const std::function<Scalar(const Eigen::Matrix<Scalar,1,3> &,const int i)> & 
      primitive,
    const Eigen::MatrixBase<Derivedorigin> & origin,
    const Scalar h0,
    const int max_depth,
    Eigen::PlainObjectBase<DerivedmV> & mV,
    Eigen::PlainObjectBase<DerivedmF> & mF);
}

#ifndef IGL_STATIC_LIBRARY
#  include "variable_radius_offset.cpp"
#endif
#endif 

