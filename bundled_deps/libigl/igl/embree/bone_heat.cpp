// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "bone_heat.h"
#include "EmbreeIntersector.h"
#include "bone_visible.h"
#include "../project_to_line_segment.h"
#include "../cotmatrix.h"
#include "../massmatrix.h"
#include "../min.h"
#include <Eigen/Sparse>

bool igl::embree::bone_heat(
  const Eigen::MatrixXd & V,
  const Eigen::MatrixXi & F,
  const Eigen::MatrixXd & C,
  const Eigen::VectorXi & P,
  const Eigen::MatrixXi & BE,
  const Eigen::MatrixXi & CE,
  Eigen::MatrixXd & W)
{
  assert(CE.rows() == 0 && "Cage edges not supported.");
  assert(C.cols() == V.cols() && "V and C should have same #cols");
  assert(BE.cols() == 2 && "BE should have #cols=2");
  assert(F.cols() == 3 && "F should contain triangles.");
  assert(V.cols() == 3 && "V should contain 3D positions.");

  const int n = V.rows();
  const int np = P.rows();
  const int nb = BE.rows();
  const int m = np + nb;

  // "double sided lighting"
  Eigen::MatrixXi FF;
  FF.resize(F.rows()*2,F.cols());
  FF << F, F.rowwise().reverse();
  // Initialize intersector
  EmbreeIntersector ei;
  ei.init(V.cast<float>(),F.cast<int>());

  typedef Eigen::Matrix<bool ,Eigen::Dynamic,1> VectorXb;
  typedef Eigen::Matrix<bool ,Eigen::Dynamic ,Eigen::Dynamic> MatrixXb;
  MatrixXb vis_mask(n,m);
  // Distances
  Eigen::MatrixXd D(n,m);
  // loop over points
  for(int j = 0;j<np;j++)
  {
    const Eigen::Vector3d p = C.row(P(j));
    D.col(j) = (V.rowwise()-p.transpose()).rowwise().norm();
    VectorXb vj;
    bone_visible(V,F,ei,p,p,vj);
    vis_mask.col(j) = vj;
  }

  // loop over bones
  for(int j = 0;j<nb;j++)
  {
    const Eigen::Vector3d s = C.row(BE(j,0));
    const Eigen::Vector3d d = C.row(BE(j,1));
    Eigen::VectorXd t,sqrD;
    project_to_line_segment(V,s,d,t,sqrD);
    D.col(np+j) = sqrD.array().sqrt();
    VectorXb vj;
    bone_visible(V,F,ei,s,d,vj);
    vis_mask.col(np+j) = vj;
  }

  assert(CE.rows() == 0 && "Cage edges not supported.");

  Eigen::MatrixXd PP = Eigen::MatrixXd::Zero(n,m);
  Eigen::VectorXd min_D;
  Eigen::VectorXd Hdiag = Eigen::VectorXd::Zero(n);
  Eigen::VectorXi J;
  igl::min(D,2,min_D,J);
  for(int i = 0;i<n;i++)
  {
    PP(i,J(i)) = 1;
    if(vis_mask(i,J(i)))
    {
      double hii = pow(min_D(i),-2.); 
      Hdiag(i) = (hii>1e10?1e10:hii);
    }
  }
  Eigen::SparseMatrix<double> Q,L,M;
  cotmatrix(V,F,L);
  massmatrix(V,F,MASSMATRIX_TYPE_DEFAULT,M);
  const auto & H = Hdiag.asDiagonal();
  Q = (-L+M*H);
  Eigen::SimplicialLLT <Eigen::SparseMatrix<double > > llt;
  llt.compute(Q);
  switch(llt.info())
  {
    case Eigen::Success:
      break;
    case Eigen::NumericalIssue:
#ifdef IGL_BONE_HEAT_DEBUG
      std::cerr<<"Error: Numerical issue."<<std::endl;
#endif
      return false;
    default:
#ifdef IGL_BONE_HEAT_DEBUG
      std::cerr<<"Error: Other."<<std::endl;
#endif
      return false;
  }

  const auto & rhs = M*H*PP;
  W = llt.solve(rhs);
  return true;
}
