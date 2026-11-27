// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/

#ifndef IGL_SUPER_FIBONACCI_H
#define IGL_SUPER_FIBONACCI_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>

namespace igl
{
  ///   super_fibonacci Generate n quaternions according to "Super-Fibonacci
  /// Spirals: Fast, Low-Discrepancy Sampling of SO(3)" [Alexa 2021]
  /// 
  /// 
  /// @param[in]  n  number of rotations to generate
  /// @param[out] q  n by 4list of unit quaternions
  template <typename DerivedQ>
  IGL_INLINE void super_fibonacci(
    const int n,
    Eigen::PlainObjectBase<DerivedQ> & Q);
}

#ifndef IGL_STATIC_LIBRARY
#include "super_fibonacci.cpp"
#endif
#endif
