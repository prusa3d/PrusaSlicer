// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#pragma once
#ifndef IGL_PREDICATES_INSPHERE_H
#define IGL_PREDICATES_INSPHERE_H

#include "../igl_inline.h"
#include <Eigen/Core>
#include "../Orientation.h"

namespace igl {
  namespace predicates {
    /// Decide whether a point is inside/outside/on a sphere.
    ///
    /// @param[in] pa  2D point on sphere
    /// @param[in] pb  2D point on sphere
    /// @param[in] pc  2D point on sphere
    /// @param[in] pd  2D point on sphere
    /// @param[in] pe  2D point query
    /// @return INSIDE if pe is inside of the sphere defined by pa, pb, pc and pd.
    ///          OUSIDE if pe is outside of the sphere.
    ///          COSPHERICAL pd is exactly on the sphere.
    ///
    /// \fileinfo
    template<typename Vector3D>
    IGL_INLINE Orientation insphere(
        const Eigen::MatrixBase<Vector3D>& pa,
        const Eigen::MatrixBase<Vector3D>& pb,
        const Eigen::MatrixBase<Vector3D>& pc,
        const Eigen::MatrixBase<Vector3D>& pd,
        const Eigen::MatrixBase<Vector3D>& pe);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "insphere.cpp"
#endif

#endif


