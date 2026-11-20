// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "collapse_edge_would_intersect_mesh.h"
#include "AABB.h"
#include "circulation.h"
#include "tri_tri_intersect.h"
#include <Eigen/Geometry>
#include <vector>
#include <algorithm>

template <
  typename Derivedp,
  typename DerivedV,
  typename DerivedF,
  typename DerivedE,
  typename DerivedEMAP,
  typename DerivedEF,
  typename DerivedEI,
  typename DerivedVB,
  typename DerivedFB>
IGL_INLINE bool igl::collapse_edge_would_intersect_mesh(
  const int e,
  const Eigen::MatrixBase<Derivedp> & p,
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const Eigen::MatrixBase<DerivedE> & E,
  const Eigen::MatrixBase<DerivedEMAP> & EMAP,
  const Eigen::MatrixBase<DerivedEF> & EF,
  const Eigen::MatrixBase<DerivedEI> & EI,
  const Eigen::MatrixBase<DerivedVB> & VB,
  const Eigen::MatrixBase<DerivedFB> & FB,
  const igl::AABB<DerivedVB,3> & tree,
  const int inf_face_id)
{
  // The faces incident on `e` that survive the collapse (their corner on the
  // collapsed edge moves to `p`). These are exactly the one-ring faces of `e`
  // minus the two faces `f1,f2` that are destroyed by the collapse.
  std::vector<int> new_one_ring;
  {
    std::vector<int> Nsv,Nsf,Ndv,Ndf;
    igl::circulation(e, true, F,EMAP,EF,EI,Nsv,Nsf);
    igl::circulation(e,false, F,EMAP,EF,EI,Ndv,Ndf);
    new_one_ring.reserve(Nsf.size()+Ndf.size());
    new_one_ring.insert(new_one_ring.end(),Nsf.begin(),Nsf.end());
    new_one_ring.insert(new_one_ring.end(),Ndf.begin(),Ndf.end());
    std::sort(new_one_ring.begin(),new_one_ring.end());
    new_one_ring.erase(
      std::unique(new_one_ring.begin(),new_one_ring.end()),new_one_ring.end());
  }
  const int f1 = EF(e,0);
  const int f2 = EF(e,1);
  new_one_ring.erase(
    std::remove(new_one_ring.begin(),new_one_ring.end(),f1),new_one_ring.end());
  new_one_ring.erase(
    std::remove(new_one_ring.begin(),new_one_ring.end(),f2),new_one_ring.end());

  const Eigen::RowVector3d pd = p.template cast<double>();

  for(const int f : new_one_ring)
  {
    if(inf_face_id >= 0 && f >= inf_face_id) { continue; }
    // Assemble the reshaped triangle, replacing the corner on the collapsed
    // edge with `p`. Remember the "fixed" (un-moved) corners so that obstacle
    // triangles which share these positions (i.e., the obstacle coincides with
    // the mesh here) can be ignored: such flush/adjacent triangles only touch
    // tangentially and should not count as intersections.
    Eigen::RowVector3d T[3];
    Eigen::AlignedBox<double,3> box;
    Eigen::RowVector3d fixed[2];
    int num_fixed = 0;
    double h2 = 0;
    for(int c = 0;c<3;c++)
    {
      if(F(f,c) == E(e,0) || F(f,c) == E(e,1))
      {
        T[c] = pd;
      }else
      {
        T[c] = V.row(F(f,c)).template cast<double>();
        fixed[num_fixed++] = T[c];
      }
      box.extend(T[c].transpose());
    }
    // Characteristic squared length of the reshaped triangle, used to build a
    // scale-relative tolerance for detecting shared vertex positions.
    h2 = std::max( (T[0]-T[1]).squaredNorm(),
          std::max((T[1]-T[2]).squaredNorm(),(T[2]-T[0]).squaredNorm()));
    const double tol2 = 1e-14 * (h2 > 0 ? h2 : 1.0);
    const auto shares_fixed_vertex = [&](const Eigen::RowVector3d & b)->bool
    {
      for(int k = 0;k<num_fixed;k++)
      {
        if((b-fixed[k]).squaredNorm() <= tol2) { return true; }
      }
      return false;
    };
    // Broad phase against the static tree.
    std::vector<const igl::AABB<DerivedVB,3>*> candidates;
    tree.append_intersecting_leaves(box,candidates);
    for(const auto * candidate : candidates)
    {
      const int g = candidate->m_primitive;
      const Eigen::RowVector3d B0 = VB.row(FB(g,0)).template cast<double>();
      const Eigen::RowVector3d B1 = VB.row(FB(g,1)).template cast<double>();
      const Eigen::RowVector3d B2 = VB.row(FB(g,2)).template cast<double>();
      // Skip obstacle triangles that share a fixed corner with the reshaped
      // triangle: these are (near-)coincident/adjacent to the mesh and can only
      // touch tangentially.
      if(shares_fixed_vertex(B0) ||
         shares_fixed_vertex(B1) ||
         shares_fixed_vertex(B2))
      {
        continue;
      }
      // Only count transversal (non-coplanar) intersections so that a mesh
      // lying flush against the static obstacle (e.g., the original surface)
      // may slide along it but not pierce through it.
      bool coplanar = false;
      Eigen::RowVector3d s,t;
      const bool hit =
        igl::tri_tri_intersection_test_3d(
          T[0],T[1],T[2], B0,B1,B2, coplanar, s,t);
      if(hit && !coplanar)
      {
        return true;
      }
    }
  }
  return false;
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template bool igl::collapse_edge_would_intersect_mesh<Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>>(int, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 1, 0, -1, 1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, igl::AABB<Eigen::Matrix<double, -1, -1, 0, -1, -1>, 3> const&, int);
#endif
