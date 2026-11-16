// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_BLOCK_SELF_INTERSECTIONS_H
#define IGL_BLOCK_SELF_INTERSECTIONS_H
#include "igl_inline.h"
#include "decimate_callback_types.h"
namespace igl
{
  /// Construct a decimation callback decorator that prevents edge collapses
  /// from introducing _self_-intersections into the mesh being decimated.
  ///
  /// The returned decorator lazily builds (and owns, for the lifetime of the
  /// wrapped callbacks) a dynamic igl::AABB tree that tracks the mesh as it is
  /// decimated. It is a thin, ownership-managing wrapper around
  /// igl::intersection_blocking_collapse_edge_callbacks that fits the generic
  /// igl::decimate / igl::qslim decorator interface.
  ///
  /// #### Example
  ///
  /// ```cpp
  /// std::vector<igl::decimate_pre_post_collapse_callbacks_decorator> decorators;
  /// decorators.push_back(igl::block_self_intersections());
  /// igl::qslim(V,F,max_m,decorators,U,G,J,I);
  /// ```
  ///
  /// @return decorator suitable for igl::decimate / igl::qslim
  ///
  /// \see decimate, qslim, block_intersections_with_input,
  ///   intersection_blocking_collapse_edge_callbacks
  IGL_INLINE decimate_pre_post_collapse_callbacks_decorator
    block_self_intersections();
}
#ifndef IGL_STATIC_LIBRARY
#  include "block_self_intersections.cpp"
#endif
#endif
