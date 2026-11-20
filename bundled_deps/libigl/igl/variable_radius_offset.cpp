// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2025 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "variable_radius_offset.h"
#include "SphereMeshWedge.h"
#include "marching_cubes.h"
#include "unique_sparse_voxel_corners.h"
#include "lipschitz_octree.h"
#include "eytzinger_aabb.h"
#include "eytzinger_aabb_sdf.h"
#include "get_seconds.h"
#include "parallel_for.h"
#include <cstdio>

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedR,
  typename Derivedorigin,
  typename DerivedmV,
  typename DerivedmF>
void igl::variable_radius_offset(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const Eigen::MatrixBase<DerivedR> & R,
    const Eigen::MatrixBase<Derivedorigin> & origin,
    const typename Derivedorigin::Scalar h0,
    const int max_depth,
    Eigen::PlainObjectBase<DerivedmV> & mV,
    Eigen::PlainObjectBase<DerivedmF> & mF)
{
  using Scalar = typename DerivedV::Scalar;
  Eigen::Matrix<Scalar,Eigen::Dynamic,3,Eigen::RowMajor> PB1,PB2;
  std::vector<igl::SphereMeshWedge<Scalar>> data;
  std::function<Scalar(const Eigen::Matrix<Scalar,1,3> &,const int i)> primitive;
  igl::variable_radius_offset(V,F,R,PB1,PB2,data,primitive);
  igl::variable_radius_offset(
    PB1,PB2,data,primitive,origin,h0,max_depth,mV,mF);
}

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedR,
  typename DerivedPB1,
  typename DerivedPB2,
  typename Scalar>
void igl::variable_radius_offset(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const Eigen::MatrixBase<DerivedR> & R,
  Eigen::PlainObjectBase<DerivedPB1> & PB1,
  Eigen::PlainObjectBase<DerivedPB2> & PB2,
  std::vector<igl::SphereMeshWedge<Scalar>> & data,
  std::function<Scalar(const Eigen::Matrix<Scalar,1,3> &,const int i)> & 
    primitive)
{
  // Bounds
  PB1.setConstant(F.rows(),3,std::numeric_limits<Scalar>::infinity());
  PB2.setConstant(F.rows(),3,-std::numeric_limits<Scalar>::infinity());
  for(int f = 0;f<F.rows();f++)
  {
    for(int c = 0;c<F.cols();c++)
    {
      for(int d = 0;d<V.cols();d++)
      {
        PB1(f,d) = std::min(PB1(f,d),V(F(f,c),d)-R(F(f,c)));
        PB2(f,d) = std::max(PB2(f,d),V(F(f,c),d)+R(F(f,c)));
      }
    }
  }

  // Precomputed data
  data.resize(F.rows());
  for(int f = 0;f<F.rows();f++)
  {
    data[f] = igl::SphereMeshWedge<Scalar>(
      V.row(F(f,0)),V.row(F(f,1)),V.row(F(f,2)),
      R(F(f,0)),R(F(f,1)),R(F(f,2)));
  }

  // I suppose primitive could just capture `data` by value. Then it owns the
  // data and we don't need to worry about `data` getting destroyed before its
  // called. I'm not sure how expensive that copy would be.
  primitive = [&](const Eigen::RowVector3d & p, const int i)
  {
    return data[i](p);
  };
}


template <
  typename DerivedPB1,
  typename DerivedPB2,
  typename Scalar,
  typename Derivedorigin,
  typename DerivedmV,
  typename DerivedmF>
void igl::variable_radius_offset(
  const Eigen::MatrixBase<DerivedPB1> & PB1,
  const Eigen::MatrixBase<DerivedPB2> & PB2,
  const std::vector<igl::SphereMeshWedge<Scalar>> & data,
  const std::function<Scalar(const Eigen::Matrix<Scalar,1,3> &,const int i)> & 
    primitive,
  const Eigen::MatrixBase<Derivedorigin> & origin,
  const Scalar h0,
  const int max_depth,
  Eigen::PlainObjectBase<DerivedmV> & mV,
  Eigen::PlainObjectBase<DerivedmF> & mF)
{
  using RowVector3S = Eigen::Matrix<Scalar,1,3>;
  Eigen::Matrix<Scalar,Eigen::Dynamic,3,Eigen::RowMajor> B1,B2;
  Eigen::VectorXi leaf;
  igl::eytzinger_aabb(PB1,PB2,B1,B2,leaf);

  const std::function<Scalar(const RowVector3S &)>
    sdf = [&](const RowVector3S & p) -> Scalar
  {
    const std::function<Scalar(const int)> primitive_p = [&](const int j)
    {
      return primitive(p,j);
    };
    Scalar f;
    igl::eytzinger_aabb_sdf<false>(p,primitive_p,B1,B2,leaf,f);
    return f;
  };
  const std::function<Scalar(const RowVector3S &)>
    udf = [&](const RowVector3S & p) -> Scalar
  {
    return std::abs(sdf(p));
    //// This made performance worse.
    //const std::function<Scalar(const int)> primitive_p = [&](const int j)
    //{
    //  const Scalar d = udTriangle( V.row(F(j,0)), V.row(F(j,1)), V.row(F(j,2)), p);
    //  const RowVector3S r(R(F(j,0)),R(F(j,1)),R(F(j,2)));
    //  const Scalar min_r = r.minCoeff();
    //  const Scalar max_r = r.maxCoeff();
    //  if(d > max_r)
    //  {
    //    return d - max_r;
    //  }else if(d < min_r)
    //  {
    //    return d - min_r;
    //  }
    //  return 0.0;
    //};
    //Scalar f;
    //igl::eytzinger_aabb_sdf(p,primitive_p,B1,B2,leaf,f);
    //return f;
  };
  Eigen::Matrix<int,Eigen::Dynamic,3,Eigen::RowMajor> ijk;
  igl::lipschitz_octree( origin,h0,max_depth,udf,ijk);

  {
    // Gather the corners of those leaf cells
    const Scalar h = h0 / (1 << max_depth);
    Eigen::Matrix<int,Eigen::Dynamic,8,Eigen::RowMajor> J;
    Eigen::Matrix<int,Eigen::Dynamic,3,Eigen::RowMajor> unique_ijk;
    Eigen::Matrix<Scalar,Eigen::Dynamic,3,Eigen::RowMajor> unique_corner_positions;
    igl::unique_sparse_voxel_corners(origin,h0,max_depth,ijk,unique_ijk,J,unique_corner_positions);
    /// Evaluate the signed distance function at the corners
    Eigen::VectorXd S(unique_corner_positions.rows());
    //for(int u = 0;u<unique_corner_positions.rows();u++)
    igl::parallel_for(
      unique_corner_positions.rows(),
      [&](const int u)
      {
        // evaluate the function at the corner
        S(u) = sdf(unique_corner_positions.row(u));
      },1000);
    // Run marching cubes on the sparse set of leaf cells
    igl::marching_cubes( S,unique_corner_positions,J, 0, mV,mF);
  }
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::variable_radius_offset<Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<double, -1, 3, 1, -1, 3>, double>(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 1, -1, 3>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1>> const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 1, -1, 3>>&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 1, -1, 3>>&, std::vector<igl::SphereMeshWedge<double>, std::allocator<igl::SphereMeshWedge<double>>>&, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&, int)>&);
template void igl::variable_radius_offset<Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, 1, 3, 1, 1, 3>, Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, 3, 0, -1, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 1, -1, 3>> const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 3, 1, -1, 3>> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3>> const&, Eigen::Matrix<double, 1, 3, 1, 1, 3>::Scalar, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 0, -1, 3>>&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 0, -1, 3>>&);
#endif
