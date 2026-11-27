#ifndef IGL_PREDICATES_POINT_IN_CONVEX_POLYGON_H
#define IGL_PREDICATES_POINT_IN_CONVEX_POLYGON_H

#include "../igl_inline.h"
#include <Eigen/Core>
#include "../Orientation.h"

namespace igl 
{
  namespace predicates 
  {
    /// Determine if the 2D point q is inside, outside, or on the boundary of the
    /// convex hull of the 2D points a,b,c,d. The points a,b,c,d may be given in
    /// any order and the convex hull may be a point, segment, triangle or
    /// quadrilateral.
    ///
    /// @param[in] q  2D query point
    /// @param[in] a  2D point possibly on the convex hull
    /// @param[in] b  2D point possibly on the convex hull
    /// @param[in] c  2D point possibly on the convex hull
    /// @param[in] d  2D point possibly on the convex hull
    /// @return Returns +1 if q is inside the convex hull, -1 if outside, and 0
    /// if on the boundary.
    template <
      typename Derivedq,
      typename Deriveda,
      typename Derivedb,
      typename Derivedc,
      typename Derivedd>
    IGL_INLINE Orientation point_in_convex_hull(
      const Eigen::MatrixBase<Derivedq> & q,
      const Eigen::MatrixBase<Deriveda> & a,
      const Eigen::MatrixBase<Derivedb> & b,
      const Eigen::MatrixBase<Derivedc> & c,
      const Eigen::MatrixBase<Derivedd> & d);
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "point_in_convex_hull.cpp"
#endif

#endif
