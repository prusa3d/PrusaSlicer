#ifndef IGL_COPYLEFT_CGAL_JOIN_COPLANAR_NEIGHBORING_FACETS_H
#define IGL_COPYLEFT_CGAL_JOIN_COPLANAR_NEIGHBORING_FACETS_H
#include "../../igl_inline.h"
namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {
      template <typename Polyhedron>
      IGL_INLINE void join_coplanar_neighboring_facets(
        Polyhedron & poly);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#include "join_coplanar_neighboring_facets.cpp"
#endif
#endif
