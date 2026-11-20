// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "swept_volume_signed_distance.h"
#include "flood_fill.h"
#include "signed_distance.h"
#include "fast_winding_number.h"
#include "AABB.h"
#include "WindingNumberAABB.h"
#include "pseudonormal_test.h"
#include "per_face_normals.h"
#include "per_vertex_normals.h"
#include "per_edge_normals.h"
#include <Eigen/Geometry>
#include <cmath>
#include <algorithm>

template <
  typename DerivedV,
  typename DerivedF,
  typename TScalar,
  typename TAlloc,
  typename DerivedGV,
  typename Derivedres,
  typename DerivedS0,
  typename DerivedS>
IGL_INLINE void igl::swept_volume_signed_distance(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
    transforms,
  const SignedDistanceType sign_type,
  const Eigen::MatrixBase<DerivedGV> & GV,
  const Eigen::MatrixBase<Derivedres> & res,
  const typename DerivedGV::Scalar h,
  const typename DerivedGV::Scalar isolevel,
  const Eigen::MatrixBase<DerivedS0> & S0,
  Eigen::PlainObjectBase<DerivedS> & S)
{
  using namespace igl;
  typedef typename DerivedV::Scalar Scalar;
  typedef typename DerivedF::Scalar Index;
  typedef typename DerivedS::Scalar SScalar;
  typedef Eigen::Matrix<Scalar,1,3> RowVector3S;
  S = S0.template cast<SScalar>();
  const bool finite_iso = std::isfinite(isolevel);
  const Scalar h_ = h;
  const Scalar isolevel_ = finite_iso ? (Scalar)isolevel : 
    std::numeric_limits<Scalar>::infinity();
  const Scalar extension = (finite_iso ? isolevel_ : (Scalar)0) + sqrt(3.0)*h_;
  Eigen::AlignedBox<Scalar,3> box(
    V.colwise().minCoeff().array()-extension,
    V.colwise().maxCoeff().array()+extension);
  // Precomputation (mesh is fixed; only the query points move)
  Eigen::Matrix<Scalar,Eigen::Dynamic,3> FN,VN,EN;
  Eigen::Matrix<Index,Eigen::Dynamic,2> E;
  Eigen::Matrix<Index,Eigen::Dynamic,1> EMAP;
  WindingNumberAABB<Scalar,Index> hier;
  igl::FastWindingNumberBVH fwn_bvh;
  AABB<DerivedV,3> tree;
  tree.init(V,F);
  switch(sign_type)
  {
    default:
      assert(false && "Unknown SignedDistanceType");
    case SIGNED_DISTANCE_TYPE_UNSIGNED:
      // do nothing
      break;
    case SIGNED_DISTANCE_TYPE_DEFAULT:
    case SIGNED_DISTANCE_TYPE_WINDING_NUMBER:
      hier.set_mesh(V,F);
      hier.grow();
      break;
    case SIGNED_DISTANCE_TYPE_FAST_WINDING_NUMBER:
      igl::fast_winding_number(V.template cast<float>().eval(),F,2,fwn_bvh);
      break;
    case SIGNED_DISTANCE_TYPE_PSEUDONORMAL:
      per_face_normals(V,F,FN);
      per_vertex_normals(V,F,PER_VERTEX_NORMALS_WEIGHTING_TYPE_ANGLE,FN,VN);
      per_edge_normals(
        V,F,PER_EDGE_NORMALS_WEIGHTING_TYPE_UNIFORM,FN,EN,E,EMAP);
      break;
  }
  for(const Eigen::Transform<TScalar,3,Eigen::Affine> & T : transforms)
  {
    const Eigen::Transform<Scalar,3,Eigen::Affine> At = 
      T.template cast<Scalar>();
    for(int g = 0;g<GV.rows();g++)
    {
      // Don't bother finding out how deep inside points are.
      if(finite_iso && S(g)==S(g) && S(g)<isolevel_-sqrt(3.0)*h_)
      {
        continue;
      }
      const RowVector3S gv =
        (GV.row(g).template cast<Scalar>() - At.translation().transpose())*
        At.linear();
      // If outside of extended box, then consider it "far away enough"
      if(finite_iso && !box.contains(gv.transpose()))
      {
        continue;
      }
      RowVector3S c,n;
      int i;
      Scalar sqrd,s = 1;
      const Scalar min_sqrd = 
        finite_iso ? 
        pow(sqrt(3.)*h_+isolevel_,2) : 
        std::numeric_limits<Scalar>::infinity();
      sqrd = tree.squared_distance(V,F,gv,min_sqrd,i,c);
      if(sqrd<min_sqrd)
      {
        // Determine sign
        switch(sign_type)
        {
          default:
            assert(false && "Unknown SignedDistanceType");
          case SIGNED_DISTANCE_TYPE_UNSIGNED:
            break;
          case SIGNED_DISTANCE_TYPE_DEFAULT:
          case SIGNED_DISTANCE_TYPE_WINDING_NUMBER:
            s = 1.-2.*hier.winding_number(gv.transpose());
            break;
          case SIGNED_DISTANCE_TYPE_FAST_WINDING_NUMBER:
          {
            const Scalar w = 
              fast_winding_number(fwn_bvh,2,gv.template cast<float>().eval());
            s = 1.-2.*std::abs(w);
            break;
          }
          case SIGNED_DISTANCE_TYPE_PSEUDONORMAL:
            pseudonormal_test(V,F,FN,VN,EN,EMAP,gv,i,c,s,n);
            break;
        }
        const SScalar sd = s*sqrt(sqrd);
        if(S(g) == S(g))
        {
          S(g) = std::min(S(g),sd);
        }else
        {
          S(g) = sd;
        }
      }
    }
  }

  if(finite_iso)
  {
    flood_fill(res,S);
  }else
  {
#ifndef NDEBUG
    // Check for nans
    std::for_each(S.data(),S.data()+S.size(),[](const SScalar s){assert(s==s);});
#endif
  }
}

template <
  typename DerivedV,
  typename DerivedF,
  typename TScalar,
  typename TAlloc,
  typename DerivedGV,
  typename Derivedres,
  typename DerivedS>
IGL_INLINE void igl::swept_volume_signed_distance(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
    transforms,
  const SignedDistanceType sign_type,
  const Eigen::MatrixBase<DerivedGV> & GV,
  const Eigen::MatrixBase<Derivedres> & res,
  const typename DerivedGV::Scalar h,
  const typename DerivedGV::Scalar isolevel,
  Eigen::PlainObjectBase<DerivedS> & S)
{
  using namespace igl;
  S = DerivedS::Constant(
    GV.rows(),1,std::numeric_limits<typename DerivedS::Scalar>::quiet_NaN());
  return swept_volume_signed_distance(
    V,F,transforms,sign_type,GV,res,h,isolevel,S,S);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
typedef Eigen::Transform<double,3,Eigen::Affine> Affine3dT;
typedef Eigen::aligned_allocator<Affine3dT> Affine3dAlignedAlloc;
typedef std::allocator<Affine3dT> Affine3dAlloc;
template void igl::swept_volume_signed_distance<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1>,
  double,
  Affine3dAlignedAlloc,
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,1,3,1,1,3>,
  Eigen::Matrix<double,-1,1,0,-1,1>,
  Eigen::Matrix<double,-1,1,0,-1,1> >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > const &,
  std::vector<Affine3dT,Affine3dAlignedAlloc> const &,
  igl::SignedDistanceType,
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,1,3,1,1,3> > const &,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::MatrixBase<Eigen::Matrix<double,-1,1,0,-1,1> > const &,
  Eigen::PlainObjectBase<Eigen::Matrix<double,-1,1,0,-1,1> > &);
template void igl::swept_volume_signed_distance<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1>,
  double,
  Affine3dAlignedAlloc,
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,1,3,1,1,3>,
  Eigen::Matrix<double,-1,1,0,-1,1> >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > const &,
  std::vector<Affine3dT,Affine3dAlignedAlloc> const &,
  igl::SignedDistanceType,
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,1,3,1,1,3> > const &,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::PlainObjectBase<Eigen::Matrix<double,-1,1,0,-1,1> > &);
template void igl::swept_volume_signed_distance<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1>,
  double,
  Affine3dAlloc,
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,1,3,1,1,3>,
  Eigen::Matrix<double,-1,1,0,-1,1>,
  Eigen::Matrix<double,-1,1,0,-1,1> >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > const &,
  std::vector<Affine3dT,Affine3dAlloc> const &,
  igl::SignedDistanceType,
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,1,3,1,1,3> > const &,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::MatrixBase<Eigen::Matrix<double,-1,1,0,-1,1> > const &,
  Eigen::PlainObjectBase<Eigen::Matrix<double,-1,1,0,-1,1> > &);
template void igl::swept_volume_signed_distance<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1>,
  double,
  Affine3dAlloc,
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,1,3,1,1,3>,
  Eigen::Matrix<double,-1,1,0,-1,1> >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > const &,
  std::vector<Affine3dT,Affine3dAlloc> const &,
  igl::SignedDistanceType,
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,1,3,1,1,3> > const &,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::PlainObjectBase<Eigen::Matrix<double,-1,1,0,-1,1> > &);
#endif
