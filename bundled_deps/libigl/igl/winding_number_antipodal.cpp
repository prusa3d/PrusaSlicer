// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2026 Philip Trettner <trettner@shapedcode.com>, Cedric Martens <cedric.martens@umontreal.ca>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "winding_number_antipodal.h"
#include "WindingNumberAntipodalScene.h"

template <
  typename Intersector,
  typename DerivedV,
  typename DerivedF,
  typename DerivedO,
  typename DerivedW>
IGL_INLINE void igl::winding_number_antipodal(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const Intersector & intersector,
  const Eigen::MatrixBase<DerivedO> & O,
  Eigen::PlainObjectBase<DerivedW> & W)
{
  using Scalar = typename DerivedV::Scalar;
  igl::WindingNumberAntipodalScene<Scalar> scene(V, F);
  scene.winding_number(intersector, O, W);
}

// No explicit template instantiations live in this file: the function is
// templated on `Intersector`, which is opaque to the core target. Concrete
// instantiations against `igl::embree::EmbreeIntersector` live in
// include/igl/embree/winding_number_antipodal.cpp (igl_embree target),
// because igl_core does not link embree.
