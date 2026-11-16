// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/

#ifndef IGL_COPYLEFT_CGAL_ORIENTED_BOUNDING_BOX_H
#define IGL_COPYLEFT_CGAL_ORIENTED_BOUNDING_BOX_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {
      /// Given a set of points compute the rotation transformation of them such
      /// that their axis-aligned bounding box is as small as possible.
      ///
      /// igl::oriented_bounding_box is often faster and better
      ///
      /// @param[in] P  #P by 3 list of point locations
      /// @param[out] R  rotation matrix
      template <typename DerivedP, typename DerivedR>
      IGL_INLINE void oriented_bounding_box(
        const Eigen::MatrixBase<DerivedP>& P,
        Eigen::PlainObjectBase<DerivedR> & R);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "oriented_bounding_box.cpp"
#endif
#endif
