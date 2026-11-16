#ifndef IGL_SWEPT_VOLUME_H
#define IGL_SWEPT_VOLUME_H
#include "igl_inline.h"
#include "signed_distance.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>
namespace igl
{
  /// Compute the surface of the swept volume of a solid object with surface
  /// (V,F) mesh under going rigid motion.
  /// 
  /// @param[in] V  #V by 3 list of mesh positions in reference pose
  /// @param[in] F  #F by 3 list of mesh indices into V
  /// @param[in] transforms  #transforms list of rigid transformations, one per
  ///     time step
  /// @param[in] sign_type  method for computing distance _sign_
  /// @param[in] grid_res  number of grid cells on the longest side containing the
  ///     motion (enough cells to cover isolevel, plus one, will also be added
  ///     on each side as padding)
  /// @param[in] isolevel  distance level to be contoured as swept volume (in
  ///     the same units as V)
  /// @param[out] SV  #SV by 3 list of mesh positions of the swept surface
  /// @param[out] SF  #SF by 3 list of mesh faces into SV
  template <
    typename DerivedV,
    typename DerivedF,
    typename TScalar,
    typename TAlloc,
    typename DerivedSV,
    typename DerivedSF>
  IGL_INLINE void swept_volume(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F,
    const std::vector<Eigen::Transform<TScalar,3,Eigen::Affine>,TAlloc> &
      transforms,
    const SignedDistanceType sign_type,
    const size_t grid_res,
    const typename DerivedV::Scalar isolevel,
    Eigen::PlainObjectBase<DerivedSV> & SV,
    Eigen::PlainObjectBase<DerivedSF> & SF);
  
}

#ifndef IGL_STATIC_LIBRARY
#  include "swept_volume.cpp"
#endif

#endif
