#include "oriented_bounding_box.h"

#include "super_fibonacci.h"
#include "parallel_for.h"

template <typename DerivedP, typename DerivedR>
IGL_INLINE void igl::oriented_bounding_box(
  const Eigen::MatrixBase<DerivedP>& P,
  const int n,
  const OrientedBoundingBoxMinimizeType minimize_type,
  Eigen::PlainObjectBase<DerivedR> & R)
{
  typedef typename DerivedP::Scalar Scalar;
  Eigen::Matrix<Scalar,Eigen::Dynamic,4,Eigen::RowMajor> Q;
  igl::super_fibonacci(n-1, Q);
  Q.conservativeResize(n, 4);
  Q.row(Q.rows()-1) << 0, 0, 0, 1;
  Eigen::Matrix<Scalar,Eigen::Dynamic,1> losses(Q.rows());
  igl::parallel_for(Q.rows(),[&](const int i)
  {
    Eigen::Quaternion<Scalar> q(Q(i,3), Q(i,0), Q(i,1), Q(i,2));
    const auto R = q.toRotationMatrix();
    const auto PR = (P*R).eval();
    Eigen::Matrix<Scalar,1,3> min_corner = PR.colwise().minCoeff();
    Eigen::Matrix<Scalar,1,3> max_corner = PR.colwise().maxCoeff();
    Eigen::Matrix<Scalar,1,3> diagonal = max_corner - min_corner;
    switch(minimize_type)
    {
      case ORIENTED_BOUNDING_BOX_MINIMIZE_VOLUME:
        losses(i) = diagonal.prod();
        break;
     case ORIENTED_BOUNDING_BOX_MINIMIZE_SURFACE_AREA:
        losses(i) = 2 * (diagonal(0) * diagonal(1) + diagonal(1) * diagonal(2) + diagonal(0) * diagonal(2));
        break;
      case ORIENTED_BOUNDING_BOX_MINIMIZE_DIAGONAL_LENGTH:
        losses(i) = diagonal.squaredNorm();
        break;
      default:
        assert(false && "Unknown minimize type");
    }
  });
  int bi;
  const Scalar best_loss = losses.minCoeff(&bi);
  R = Eigen::Quaternion<Scalar>(Q(bi,3), Q(bi,0), Q(bi,1), Q(bi,2)).toRotationMatrix().eval();
}

#ifdef IGL_STATIC_LIBRARY
// Explicit instantiation of template function
template void igl::oriented_bounding_box<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, 3, 3, 0, 3, 3>>(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1>> const&, int, igl::OrientedBoundingBoxMinimizeType, Eigen::PlainObjectBase<Eigen::Matrix<double, 3, 3, 0, 3, 3>>&);
#endif 
