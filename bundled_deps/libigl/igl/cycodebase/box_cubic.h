#ifndef IGL_CYCODEBASE_BOX_CUBIC_H
#define IGL_CYCODEBASE_BOX_CUBIC_H

#include "../igl_inline.h"
#include <Eigen/Core>

namespace igl {
  namespace cycodebase {
    /// Compute the min/max box corners tightly containing a given cubic bezier
    /// curve.
    ///
    /// @param[in] C  4 by dim matrix of control points defining the cubic bezier
    /// curve
    /// @param[out] B1  1 by dim min corner of the bounding box
    /// @param[out] B2  1 by dim max corner of the bounding box
    ///
    /// \see igl::box_simplices
    template < 
      typename DerivedC, 
      typename DerivedB>
    IGL_INLINE void box_cubic(
      const Eigen::MatrixBase<DerivedC>& C,
      Eigen::PlainObjectBase<DerivedB>& B1,
      Eigen::PlainObjectBase<DerivedB>& B2);
    /// \brief overload
    ///
    /// @param[in] P  #P by dim matrix of control point locations
    /// @param[in] C  #C by 4 matrix of indices into P defining the cubics
    /// @param[out] B1  #C by dim matrix of min corners of the bounding boxes
    /// @param[out] B2  #C by dim matrix of max corners of the bounding boxes
    template < 
      typename DerivedP, 
      typename DerivedC, 
      typename DerivedB>
    IGL_INLINE void box_cubic(
      const Eigen::MatrixBase<DerivedP>& P,
      const Eigen::MatrixBase<DerivedC>& C,
      Eigen::PlainObjectBase<DerivedB>& B1,
      Eigen::PlainObjectBase<DerivedB>& B2);
  }
}

#ifndef IGL_STATIC_LIBRARY
  #include "box_cubic.cpp"
#endif
#endif
