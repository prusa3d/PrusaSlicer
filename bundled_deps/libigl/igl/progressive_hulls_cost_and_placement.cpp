// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "progressive_hulls_cost_and_placement.h"
#include "project_to_halfspace_intersection.h"
#include "unique.h"
#include "circulation.h"
#include <Eigen/Geometry>
#include <cassert>
#include <vector>
#include <limits>
#include <algorithm>

IGL_INLINE void igl::progressive_hulls_cost_and_placement(
  const int e,
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const Eigen::MatrixXi & E,
  const Eigen::VectorXi & EMAP,
  const Eigen::MatrixXi & EF,
  const Eigen::MatrixXi & EI,
  double & cost,
  Eigen::RowVectorXd & p)
{
  // Controls the amount of quadratic energy to add (too small will introduce
  // instabilities and flaps)
  const double w = 0.1;

  assert(V.cols() == 3 && "V.cols() should be 3");
  // Gather list of unique face neighbors
  std::vector<int> Nall =  circulation(e, true,EMAP,EF,EI);
  std::vector<int> Nother= circulation(e,false,EMAP,EF,EI);
  Nall.insert(Nall.end(),Nother.begin(),Nother.end());
  std::vector<int> N;
  igl::unique(Nall,N);
  // Gather:
  //   A  #N by 3 normals scaled by area,
  //   D  #N determinants of matrix formed by points as columns
  //   B  #N point on plane dot normal
  Eigen::MatrixXd A(N.size(),3);
  Eigen::VectorXd D(N.size());
  Eigen::VectorXd B(N.size());
  for(int i = 0;i<N.size();i++)
  {
    const int f = N[i];
    const Eigen::RowVector3d & v01 = V.row(F(f,1))-V.row(F(f,0));
    const Eigen::RowVector3d & v20 = V.row(F(f,2))-V.row(F(f,0));
    A.row(i) = v01.cross(v20);
    B(i) = V.row(F(f,0)).dot(A.row(i));
    D(i) =
      (Eigen::Matrix3d()<< V.row(F(f,0)), V.row(F(f,1)), V.row(F(f,2)))
      .finished().determinant();
  }
  // linear objective
  Eigen::Vector3d f = A.colwise().sum().transpose();
  Eigen::VectorXd x;

  bool success = false;
  {
    Eigen::RowVectorXd mid = 0.5*(V.row(E(e,0))+V.row(E(e,1)));
    // min 0.5*w*||y||^2 + g0'y  s.t. Ay>=B  <=>  project q=-g0/w onto Ay>=B.
    Eigen::Vector3d g0 = (1.-w)*f - w*mid.transpose();
    Eigen::Vector3d q_world = -g0/w;

    // Local normalization frame: origin at the edge midpoint, scale from
    // the edge length, unit-normalized plane normals. Solving directly in
    // raw mesh-frame coordinates (large, unnormalized area-scaled normals
    // and world-scale offsets) causes the solver's KKT/rank tolerances to
    // trip spuriously on otherwise-feasible collapses.
    const double edge_len = (V.row(E(e,0))-V.row(E(e,1))).norm();
    const double s = std::max(edge_len, 1e-9);
    const Eigen::Vector3d c = mid.transpose();

    Eigen::MatrixXd A_local(N.size(),3);
    Eigen::VectorXd B_local(N.size());
    for(int i = 0;i<N.size();i++)
    {
      const double norm_a = A.row(i).norm();
      if(norm_a < 1e-12)
      {
        A_local.row(i).setZero();
        B_local(i) = B(i) - A.row(i).dot(c);
        continue;
      }
      A_local.row(i) = A.row(i)/norm_a;
      B_local(i) = (B(i) - A.row(i).dot(c))/(s*norm_a);
    }
    const Eigen::Vector3d q_local = (q_world - c)/s;

    igl::HalfspaceProjectionOptions<double> options;
    options.eps_rank = 1e-9;
    options.eps_feasible = 1e-8;
    options.eps_kkt = 1e-7;
    igl::HalfspaceProjectionResult<double,3> result;
    const auto status = igl::project_to_halfspace_intersection<double,3>(
      q_local,A_local,B_local,options,result);
    success = (status == igl::HalfspaceProjectionStatus::SUCCESS);
    if(success)
    {
      x = c + s*result.p;
    }

    if(x.size() == 3)
    {
      cost  = (1.-w)*(1./6.)*(x.dot(f) - D.sum()) +
        w*(x.transpose()-mid).squaredNorm() +
        w*(V.row(E(e,0))-V.row(E(e,1))).norm();
    }
  }

  // A x >= B
  // A x - B >=0
  // This is annoyingly necessary. Seems the solver is letting some garbage
  // slip by.
  success = success && x.size() == 3 && ((A*x-B).minCoeff()>-1e-10);
  if(success)
  {
    p = x.transpose();
    //assert(cost>=0 && "Cost should be positive");
  }else
  {
    cost = std::numeric_limits<double>::infinity();
    p = Eigen::RowVectorXd::Constant(1,3,std::nan("inf-cost"));
  }
}
