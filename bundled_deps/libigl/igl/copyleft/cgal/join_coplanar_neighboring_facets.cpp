#include "join_coplanar_neighboring_facets.h"
#include <CGAL/Polyhedron_3.h>

template <typename Polyhedron>
IGL_INLINE void igl::copyleft::cgal::join_coplanar_neighboring_facets(
  Polyhedron & poly)
{
  using Plane_3 = typename Polyhedron::Traits::Plane_3;
  for(auto iter = poly.edges_begin(); iter != poly.edges_end();)
  {
    auto e = iter++;
    auto f1 = e->facet();
    assert(f1 != nullptr && "CGAL Polyhedron should have a facet on both sides of each edge");
    auto f2 = e->opposite()->facet();
    if(f2 == nullptr)
    {
      // boundary edge, skip
      continue;
    }
    auto p1 = Plane_3(
      f1->halfedge()->vertex()->point(), 
      f1->halfedge()->next()->vertex()->point(), 
      f1->halfedge()->next()->next()->vertex()->point());
    auto p2 = Plane_3(
      f2->halfedge()->vertex()->point(), 
      f2->halfedge()->next()->vertex()->point(), 
      f2->halfedge()->next()->next()->vertex()->point());
    if(p1 == p2)
    {
      poly.join_facet(e);
    }
  }
}


#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
#include <CGAL/Polyhedron_items_with_id_3.h>
#include <CGAL/Simple_cartesian.h>
template void igl::copyleft::cgal::join_coplanar_neighboring_facets<CGAL::Polyhedron_3<CGAL::Simple_cartesian<double>, CGAL::Polyhedron_items_with_id_3, CGAL::HalfedgeDS_default, std::allocator<int>>>(CGAL::Polyhedron_3<CGAL::Simple_cartesian<double>, CGAL::Polyhedron_items_with_id_3, CGAL::HalfedgeDS_default, std::allocator<int>>&);
#endif
