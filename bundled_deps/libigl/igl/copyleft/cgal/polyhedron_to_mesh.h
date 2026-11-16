// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_COPYLEFT_CGAL_POLYHEDRON_TO_MESH_H
#define IGL_COPYLEFT_CGAL_POLYHEDRON_TO_MESH_H
#include "../../igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {
      /// Convert a CGAL Polyhedron (assumed to be all triangles) to a mesh (V,F)
      ///
      /// @tparam Polyhedron  CGAL Polyhedron type (e.g. Polyhedron_3)
      /// @param[in] poly  cgal polyhedron
      /// @param[out] V  #V by 3 list of vertex positions
      /// @param[out] F  #F by 3 list of triangle indices
      ///
      template <
        typename Polyhedron,
        typename DerivedV,
        typename DerivedF>
      IGL_INLINE void polyhedron_to_mesh(
        const Polyhedron & poly,
        Eigen::PlainObjectBase<DerivedV> & V,
        Eigen::PlainObjectBase<DerivedF> & F);
      /// Convert a CGAL Polyhedron to a polygon mesh (V,I,C)
      ///
      /// @tparam Polyhedron  CGAL Polyhedron type (e.g. Polyhedron_3)
      /// @param[in] poly  cgal polyhedron
      /// @param[out] V  #V by 3 list of vertex positions
      /// @param[out] I  #I vectorized list of polygon corner indices into rows of some matrix V
      /// @param[out] C  #polygons+1 list of cumulative polygon sizes so that C(i+1)-C(i) =
      ///     size of the ith polygon, and so I(C(i)) through I(C(i+1)-1) are the
      ///     indices of the ith polygon
      template <
        typename Polyhedron,
        typename DerivedV,
        typename DerivedI,
        typename DerivedC
        >
      IGL_INLINE void polyhedron_to_mesh(
        const Polyhedron & poly,
        Eigen::PlainObjectBase<DerivedV> & V,
        Eigen::PlainObjectBase<DerivedI> & I,
        Eigen::PlainObjectBase<DerivedC> & C);
    }
  }
}
#ifndef IGL_STATIC_LIBRARY
#  include "polyhedron_to_mesh.cpp"
#endif

#endif
