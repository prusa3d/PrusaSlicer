// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2026 Philip Trettner <trettner@shapedcode.com>, Cedric Martens <cedric.martens@umontreal.ca>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_WINDING_NUMBER_ANTIPODAL_H
#define IGL_WINDING_NUMBER_ANTIPODAL_H

#include "igl_inline.h"

#include <Eigen/Core>

namespace igl
{
  /// Generalized winding number via the Antipodal Method
  /// [Martens, Trettner, Bessmeltsev 2026, "The Antipodal Method: Fast,
  /// Accurate, and Robust 3D Generalized Winding Numbers", SIGGRAPH 2026,
  /// https://arxiv.org/abs/2605.01536].
  ///
  /// Project: https://martenscedric.github.io/academic-page/publications/antipodal_wn.html
  ///
  /// One-shot convenience: builds a `WindingNumberAntipodalScene<Scalar>`
  /// (with `Scalar` deduced from `DerivedV`) and runs the batch query against
  /// the supplied `Intersector`. For repeated queries on the same mesh,
  /// construct the scene once and call its `winding_number(...)` member
  /// directly.
  ///
  /// Mirrors the API shape of `igl::winding_number(V, F, O, W)`.
  ///
  /// @param[in]  V            #V by 3 list of vertex positions
  /// @param[in]  F            #F by 3 list of triangle indices
  /// @param[in]  intersector  Concept-compatible intersector built over (V, F).
  ///                          `igl::embree::EmbreeIntersector` satisfies the
  ///                          concept and works out of the box. Just call
  ///                          its `init(V.cast<float>(), F.cast<int>())`.
  /// @param[in]  O            #O by 3 list of query points
  /// @param[out] W            #O by 1 list of winding numbers
  ///
  /// \see WindingNumberAntipodalScene
  /// \see igl::embree::EmbreeIntersector::signedIntersectionsRay
  template <
    typename Intersector,
    typename DerivedV,
    typename DerivedF,
    typename DerivedO,
    typename DerivedW>
  IGL_INLINE void winding_number_antipodal(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Intersector & intersector,
    const Eigen::MatrixBase<DerivedO> & O,
    Eigen::PlainObjectBase<DerivedW> & W);
}

#ifndef IGL_STATIC_LIBRARY
#  include "winding_number_antipodal.cpp"
#endif

#endif
