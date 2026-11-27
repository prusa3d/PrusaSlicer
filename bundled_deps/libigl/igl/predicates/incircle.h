// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#pragma once
#ifndef IGL_PREDICATES_INCIRCLE_H
#define IGL_PREDICATES_INCIRCLE_H

#include "../igl_inline.h"
#include <Eigen/Core>
#include "../Orientation.h"

namespace igl {
  namespace predicates {
    /// Decide whether a point is inside/outside/on a circle.
    ///
    /// @param[in] pa  2D point on circle
    /// @param[in] pb  2D point on circle
    /// @param[in] pc  2D point on circle
    /// @param[in] pd  2D point query
    /// @return INSIDE if pd is inside of the circle defined by pa, pb and pc.
    ///          OUSIDE if pd is outside of the circle.
    ///          COCIRCULAR pd is exactly on the circle.
    ///
    /// \fileinfo
    template<typename Vector2D>
    IGL_INLINE Orientation incircle(
        const Eigen::MatrixBase<Vector2D>& pa,
        const Eigen::MatrixBase<Vector2D>& pb,
        const Eigen::MatrixBase<Vector2D>& pc,
        const Eigen::MatrixBase<Vector2D>& pd);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "incircle.cpp"
#endif

#endif

