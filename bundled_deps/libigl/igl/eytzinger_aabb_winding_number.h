#ifndef IGL_EYTZINGER_AABB_WINDING_NUMBER_H
#define IGL_EYTZINGER_AABB_WINDING_NUMBER_H
#include "igl_inline.h"
#include <Eigen/Core>

namespace igl
{
  /// Given a query point, compute its (exact) winding number with respect to a
  /// set of segments stored in an Eytzinger AABB tree with winding number data
  /// structure. 
  ///
  /// @param[in] p  #1 by 2 query point
  /// @param[in] V  #V by 2 list of vertex positions
  /// @param[in] E  #E by 2 list of segment endpoint vertex indices
  /// @param[in] B1  #B by dim list of minimum corners of the Eytzinger AABBs
  /// @param[in] B2  #B by dim list of maximum corners of the Eytzinger AABBs
  /// @param[out] leaf #B list of leaf indices, -1 indicates internal node, -2
  /// indicates empty node
  /// @param[out] I  #I streamed list of vertex indices I=[(s₀,s₁), (s₂,s₃), ...]
  /// @param[out] C  #C list of cumulative counts into I for each internal node,
  /// so that the pairs for internal node i are found in I[C(i):C(i+1)-1]
  /// @param[out] wn  winding number of p with respect to the segments
  ///
  /// \see igl::eytzinger_aabb
  ///
  template <
    typename Derivedp,
    typename DerivedV,
    typename DerivedE,
    typename DerivedB,
    typename Derivedleaf,
    typename DerivedI,
    typename DerivedC>
  IGL_INLINE void eytzinger_aabb_winding_number(
    const Eigen::MatrixBase<Derivedp> & p,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedE> & E,
    const Eigen::MatrixBase<DerivedB> & B1,
    const Eigen::MatrixBase<DerivedB> & B2,
    const Eigen::MatrixBase<Derivedleaf> & leaf,
    const Eigen::MatrixBase<DerivedI> & I,
    const Eigen::MatrixBase<DerivedC> & C,
    typename DerivedV::Scalar & wn);
  template <
    typename Derivedp,
    typename DerivedV,
    typename DerivedB,
    typename Derivedleaf,
    typename DerivedI,
    typename DerivedC>
  IGL_INLINE void eytzinger_aabb_winding_number(
    const Eigen::MatrixBase<Derivedp> & p,
    const Eigen::MatrixBase<DerivedV> & V,
    const std::function<typename DerivedV::Scalar(const int)> & primitive,
    const Eigen::MatrixBase<DerivedB> & B1,
    const Eigen::MatrixBase<DerivedB> & B2,
    const Eigen::MatrixBase<Derivedleaf> & leaf,
    const Eigen::MatrixBase<DerivedI> & I,
    const Eigen::MatrixBase<DerivedC> & C,
    typename DerivedV::Scalar & wn);
}

#ifndef IGL_STATIC_LIBRARY
#include "eytzinger_aabb_winding_number.cpp"
#endif

#endif

