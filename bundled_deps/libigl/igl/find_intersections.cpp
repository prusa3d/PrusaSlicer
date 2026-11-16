// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2024 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "find_intersections.h"
#include "AABB.h"
#include "tri_tri_intersect.h"
#include "parallel_for.h"
#include <Eigen/Geometry>
#include <atomic>
#include <mutex>
#include <vector>

template <
  typename DerivedV1,
  typename DerivedF1,
  typename DerivedV2,
  typename DerivedF2,
  typename DerivedIF,
  typename DerivedCP>
IGL_INLINE bool igl::find_intersections(
  const igl::AABB<DerivedV1,3> & tree1,
  const Eigen::MatrixBase<DerivedV1> & V1,
  const Eigen::MatrixBase<DerivedF1> & F1,
  const Eigen::MatrixBase<DerivedV2> & V2,
  const Eigen::MatrixBase<DerivedF2> & F2,
  const bool first_only,
  Eigen::PlainObjectBase<DerivedIF> & IF,
  Eigen::PlainObjectBase<DerivedCP> & CP)
{
  using Scalar = typename DerivedV1::Scalar;
  using RowVector3S = Eigen::Matrix<Scalar,1,3>;
  // Parallelize the broad+narrow phase over the second mesh's faces; appends are
  // rare (only on actual intersections) so a single mutex is cheap. `first_only`
  // early-outs: once a hit is recorded, queued/other tasks bail immediately.
  //
  // igl::parallel_for spawns a fresh thread pool per call (~ms of create/join
  // overhead), so only go parallel once there is enough work to amortize it.
  // Per-face work here is a cheap AABB box query plus a few triangle-triangle
  // tests; empirically the parallel path only starts paying off past ~2e4 faces
  // (much later for well-separated meshes with no candidates), so keep smaller
  // queries — the common case, including early-out detection — serial and fast.
  const size_t min_parallel = 20000;
  std::vector<int> f1s, f2s;
  std::vector<char> cps;
  std::mutex mtx;
  std::atomic<bool> hit(false);
  igl::parallel_for(F2.rows(),[&](const int f2)
  {
    if(first_only && hit.load(std::memory_order_relaxed)) { return; }
    const RowVector3S B0 = V2.row(F2(f2,0));
    const RowVector3S B1 = V2.row(F2(f2,1));
    const RowVector3S B2 = V2.row(F2(f2,2));
    Eigen::AlignedBox<Scalar,3> box;
    box.extend(B0.transpose()).extend(B1.transpose()).extend(B2.transpose());
    std::vector<const igl::AABB<DerivedV1,3>*> candidates;
    tree1.append_intersecting_leaves(box,candidates);
    for(const auto * candidate : candidates)
    {
      if(first_only && hit.load(std::memory_order_relaxed)) { return; }
      const int f1 = candidate->m_primitive;
      const RowVector3S A0 = V1.row(F1(f1,0));
      const RowVector3S A1 = V1.row(F1(f1,1));
      const RowVector3S A2 = V1.row(F1(f1,2));
      bool coplanar = false;
      RowVector3S s,t;
      if(igl::tri_tri_intersection_test_3d(A0,A1,A2,B0,B1,B2,coplanar,s,t))
      {
        std::lock_guard<std::mutex> lock(mtx);
        if(first_only && !f1s.empty()) { return; } // someone beat us to it
        f1s.push_back(f1);
        f2s.push_back(f2);
        cps.push_back(coplanar?1:0);
        if(first_only) { hit.store(true,std::memory_order_relaxed); return; }
      }
    }
  },min_parallel);
  const int n = (int)f1s.size();
  IF.resize(n,2);
  CP.resize(n);
  for(int i = 0;i<n;i++){ IF(i,0) = f1s[i]; IF(i,1) = f2s[i]; CP(i) = cps[i]; }
  return n > 0;
}

template <
  typename DerivedV1,
  typename DerivedF1,
  typename DerivedV2,
  typename DerivedF2,
  typename DerivedIF,
  typename DerivedCP>
IGL_INLINE bool igl::find_intersections(
  const Eigen::MatrixBase<DerivedV1> & V1,
  const Eigen::MatrixBase<DerivedF1> & F1,
  const Eigen::MatrixBase<DerivedV2> & V2,
  const Eigen::MatrixBase<DerivedF2> & F2,
  const bool first_only,
  Eigen::PlainObjectBase<DerivedIF> & IF,
  Eigen::PlainObjectBase<DerivedCP> & CP)
{
  igl::AABB<DerivedV1,3> tree1;
  tree1.init(V1,F1);
  return find_intersections(tree1,V1,F1,V2,F2,first_only,IF,CP);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template bool igl::find_intersections<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Array<bool, -1, 1, 0, -1, 1>>(igl::AABB<Eigen::Matrix<double, -1, -1, 0, -1, -1>, 3> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, bool, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::PlainObjectBase<Eigen::Array<bool, -1, 1, 0, -1, 1>>&);
template bool igl::find_intersections<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Array<bool, -1, 1, 0, -1, 1>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>> const&, bool, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1>>&, Eigen::PlainObjectBase<Eigen::Array<bool, -1, 1, 0, -1, 1>>&);
#endif
