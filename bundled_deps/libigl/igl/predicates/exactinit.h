// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2019 Qingnan Zhou <qnzhou@gmail.com>
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#pragma once
#ifndef IGL_PREDICATES_EXACTINIT_H
#define IGL_PREDICATES_EXACTINIT_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl {
  namespace predicates {
    /// Initialize internal variable used by predciates. Must be called before
    /// using exact predicates. It is safe to call this function from multiple
    /// threads.
    ///
    /// \fileinfo
    IGL_INLINE void exactinit();
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "exactinit.cpp"
#endif

#endif

