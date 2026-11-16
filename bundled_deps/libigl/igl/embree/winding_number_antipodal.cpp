// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2026 Philip Trettner <trettner@shapedcode.com>, Cedric Martens <cedric.martens@umontreal.ca>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
//
// Embree-target satellite that emits explicit template instantiations of
// `igl::winding_number_antipodal` against `igl::embree::EmbreeIntersector`.
// igl_core does not link embree, so these instantiations cannot live in
// include/igl/winding_number_antipodal.cpp. In header-only mode this file
// is empty after preprocessing.
#include "../winding_number_antipodal.h"

#ifdef IGL_STATIC_LIBRARY
// Pull in the function body so we can instantiate it here. In static-lib
// mode the header does not transitively include the body.
#include "../winding_number_antipodal.cpp"
#include "EmbreeIntersector.h"

// Mirror of the (V, F, O, W) instantiations in include/igl/winding_number.cpp,
// adapted to the antipodal API (3D triangle meshes only, with EmbreeIntersector
// as the intersector).
template void igl::winding_number_antipodal<igl::embree::EmbreeIntersector, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::embree::EmbreeIntersector const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&);
template void igl::winding_number_antipodal<igl::embree::EmbreeIntersector, Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<double, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::embree::EmbreeIntersector const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&);
template void igl::winding_number_antipodal<igl::embree::EmbreeIntersector, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::embree::EmbreeIntersector const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, 1, 0, -1, 1> >&);
template void igl::winding_number_antipodal<igl::embree::EmbreeIntersector, Eigen::Matrix<float, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, 3, 0, -1, 3>, Eigen::Matrix<float, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, igl::embree::EmbreeIntersector const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, 1, 0, -1, 1> >&);
#endif
