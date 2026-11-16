#include "swept_volume.h"
#include "swept_volume_bounding_box.h"
#include "swept_volume_signed_distance.h"
#include "voxel_grid.h"
#include "marching_cubes.h"
#include <algorithm>
#include <cmath>

template <
  typename DerivedV,
  typename DerivedF,
  typename TScalar,
  typename TAlloc,
  typename DerivedSV,
  typename DerivedSF>
IGL_INLINE void igl::swept_volume(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
    transforms,
  const SignedDistanceType sign_type,
  const size_t grid_res,
  const typename DerivedV::Scalar isolevel,
  Eigen::PlainObjectBase<DerivedSV> & SV,
  Eigen::PlainObjectBase<DerivedSF> & SF)
{
  using namespace igl;

  Eigen::AlignedBox<TScalar,3> Mbox;
  swept_volume_bounding_box(V,transforms,Mbox);

  // Grid spacing is determined by the longest side of the motion's bounding
  // box; padding does not affect it.
  const TScalar h = Mbox.diagonal().maxCoeff()/(TScalar)(grid_res-1);
  // Amount of padding: pad*h should be >= isolevel
  const int pad = std::max((int)std::ceil(isolevel/h),0)+1;
  // number of vertices on the largest side
  const int s = grid_res+2*pad;

  // create grid
  Eigen::Matrix<int,1,3> res;
  Eigen::Matrix<TScalar,Eigen::Dynamic,Eigen::Dynamic> GV;
  voxel_grid(Mbox,s,pad,GV,res);

  // compute values
  Eigen::Matrix<TScalar,Eigen::Dynamic,1> S;
  swept_volume_signed_distance(
    V,F,transforms,sign_type,GV,res,h,isolevel,S);
  S.array()-=isolevel;
  marching_cubes(S,GV,res(0),res(1),res(2),0,SV,SF);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
typedef Eigen::Transform<double,3,Eigen::Affine> Affine3dT;
typedef Eigen::aligned_allocator<Affine3dT> Affine3dAlignedAlloc;
typedef std::allocator<Affine3dT> Affine3dAlloc;
template void igl::swept_volume<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1>,
  double,
  Affine3dAlignedAlloc,
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1> >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > const &,
  std::vector<Affine3dT,Affine3dAlignedAlloc> const &,
  igl::SignedDistanceType,
  size_t,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::PlainObjectBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > &,
  Eigen::PlainObjectBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > &);
template void igl::swept_volume<
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1>,
  double,
  Affine3dAlloc,
  Eigen::Matrix<double,-1,-1,0,-1,-1>,
  Eigen::Matrix<int,-1,-1,0,-1,-1> >(
  Eigen::MatrixBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > const &,
  Eigen::MatrixBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > const &,
  std::vector<Affine3dT,Affine3dAlloc> const &,
  igl::SignedDistanceType,
  size_t,
  Eigen::Matrix<double,-1,-1,0,-1,-1>::Scalar,
  Eigen::PlainObjectBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > &,
  Eigen::PlainObjectBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > &);
#endif
