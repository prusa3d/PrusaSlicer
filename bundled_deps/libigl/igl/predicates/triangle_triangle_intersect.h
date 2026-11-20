#ifndef IGL_PREDICATES_TRIANGLE_TRIANGLE_INTERSECT_H
#define IGL_PREDICATES_TRIANGLE_TRIANGLE_INTERSECT_H
#include "../igl_inline.h"

namespace igl
{
  namespace predicates
  {
    /// Triangle-triangle intersection test using exact predicates.
    ///
    /// @param[in] a1 First vertex of triangle A.
    /// @param[in] a2 Second vertex of triangle A.
    /// @param[in] a3 Third vertex of triangle A.
    /// @param[in] b1 First vertex of triangle B.
    /// @param[in] b2 Second vertex of triangle B.
    /// @param[in] b3 Third vertex of triangle B.
    /// @param[out] coplanar True if the triangles are coplanar.
    /// @return True if the triangles intersect.
    ///
    template <typename Vector3D>
    IGL_INLINE  bool triangle_triangle_intersect(
      const Vector3D & a1,
      const Vector3D & a2,
      const Vector3D & a3,
      const Vector3D & b1,
      const Vector3D & b2,
      const Vector3D & b3,
      bool & coplanar);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "triangle_triangle_intersect.cpp"
#endif

#endif 
