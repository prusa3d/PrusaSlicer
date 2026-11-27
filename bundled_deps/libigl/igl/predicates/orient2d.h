// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#pragma once
#ifndef IGL_PREDICATES_ORIENT2D_H
#define IGL_PREDICATES_ORIENT2D_H

#include "../igl_inline.h"
#include <Eigen/Core>
#include "../Orientation.h"

namespace igl {
  namespace predicates {
    /// Compute the orientation of the triangle formed by pa, pb, pc.
    ///
    /// @param[in] pa  2D point on line
    /// @param[in] pb  2D point on line
    /// @param[in] pc  2D query point.
    /// @return POSITIVE if pa, pb, pc are counterclockwise oriented.
    ///          NEGATIVE if they are clockwise oriented.
    ///          COLLINEAR if they are collinear.
    ///
    /// \fileinfo
    template<
      typename Derivedpa,
      typename Derivedpb,
      typename Derivedpc>
    IGL_INLINE Orientation orient2d(
        const Eigen::MatrixBase<Derivedpa>& pa,
        const Eigen::MatrixBase<Derivedpb>& pb,
        const Eigen::MatrixBase<Derivedpc>& pc);
    /// Compute the orientation of the tetrahedron formed by each 4-tuple of
    /// points
    ///
    /// @param[in] A  #P|1 by 3 matrix of 3D points
    /// @param[in] B  #P|1 by 3 matrix of 3D points
    /// @param[in] C  #P|1 by 3 matrix of 3D points
    /// @param[out] R  #P vector of orientations
    ///
    template 
      <typename DerivedA,
       typename DerivedB,
       typename DerivedC,
       typename DerivedR>
    IGL_INLINE void orient2d(
        const Eigen::MatrixBase<DerivedA>& A,
        const Eigen::MatrixBase<DerivedB>& B,
        const Eigen::MatrixBase<DerivedC>& C,
        Eigen::PlainObjectBase<DerivedR>& R);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "orient2d.cpp"
#endif

#endif
