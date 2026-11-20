// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#pragma once
#ifndef IGL_ORIENTATION_H
#define IGL_ORIENTATION_H

#include "igl_inline.h"
#include <Eigen/Core>

namespace igl {
  /// Types of orientations and other predicate results.
  ///
  /// \fileinfo
  enum class Orientation {
    POSITIVE=1, INSIDE=1,
    NEGATIVE=-1, OUTSIDE=-1,
    COLLINEAR=0, COPLANAR=0, COCIRCULAR=0, COSPHERICAL=0, DEGENERATE=0
  };
}


#endif
