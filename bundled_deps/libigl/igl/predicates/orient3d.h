// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#pragma once
#ifndef IGL_PREDICATES_ORIENT3D_H
#define IGL_PREDICATES_ORIENT3D_H

#include "../igl_inline.h"
#include <Eigen/Core>
#include "../Orientation.h"

namespace igl {
  namespace predicates {
    /// Compute the orientation of the tetrahedron formed by pa, pb, pc, pd.
    ///
    /// @param[in] pa  3D point on plane
    /// @param[in] pb  3D point on plane
    /// @param[in] pc  3D point on plane
    /// @param[in] pd  3D query point 
    ///  @return POSITIVE if pd is "below" the oriented plane formed by pa, pb and pc.
    ///          NEGATIVE if pd is "above" the plane.
    ///          COPLANAR if pd is on the plane.
    ///
    /// \fileinfo
    template<typename Vector3D>
    IGL_INLINE Orientation orient3d(
        const Eigen::MatrixBase<Vector3D>& pa,
        const Eigen::MatrixBase<Vector3D>& pb,
        const Eigen::MatrixBase<Vector3D>& pc,
        const Eigen::MatrixBase<Vector3D>& pd);
    /// Compute the orientation of the tetrahedron formed by each 4-tuple of
    /// points
    ///
    /// @param[in] A  #P|1 by 3 matrix of 3D points
    /// @param[in] B  #P|1 by 3 matrix of 3D points
    /// @param[in] C  #P|1 by 3 matrix of 3D points
    /// @param[in] D  #P|1 by 3 matrix of 3D points
    /// @param[out] R  #P vector of orientations
    ///
    template 
      <typename DerivedA,
       typename DerivedB,
       typename DerivedC,
       typename DerivedD,
       typename DerivedR>
    IGL_INLINE void orient3d(
        const Eigen::MatrixBase<DerivedA>& A,
        const Eigen::MatrixBase<DerivedB>& B,
        const Eigen::MatrixBase<DerivedC>& C,
        const Eigen::MatrixBase<DerivedD>& D,
        Eigen::PlainObjectBase<DerivedR>& R);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "orient3d.cpp"
#endif

#endif

