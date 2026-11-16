#include "point_in_convex_hull.h"
#include "orient2d.h"
#include <algorithm>
#include <cassert>

// Lightly edited LLM code.

namespace 
{
  using igl::Orientation;
template <typename Scalar>
struct SH
{
  static inline int osign(const Orientation o)
  {
    using namespace igl::predicates;
    switch(o)
    {
      case Orientation::POSITIVE:  return +1;
      case Orientation::NEGATIVE:  return -1;
      case Orientation::COLLINEAR: return  0;
    }
    return 0;
  }
  
  static inline bool between_1d(const Scalar x, Scalar lo, Scalar hi)
  {
    if(lo > hi) std::swap(lo, hi);
    return (lo <= x) && (x <= hi);
  }

  // Assumes q is collinear with segment endpoints (by exact orient2d).
  static inline bool on_segment_collinear(
    const Scalar qx, const Scalar qy,
    const Scalar ax, const Scalar ay,
    const Scalar bx, const Scalar by)
  {
    // Use dominant axis to avoid issues with vertical-ish segments.
    const Scalar dx = std::abs(bx - ax);
    const Scalar dy = std::abs(by - ay);
    if(dx >= dy) return between_1d(qx, ax, bx);
    else         return between_1d(qy, ay, by);
  }

  // Closed triangle membership using only cached signs; handles degenerate triples.
  static inline bool in_triangle_cached(
    const int o_ab, const int o_bc, const int o_ca,
    const Scalar qx, const Scalar qy,
    const Scalar ax, const Scalar ay,
    const Scalar bx, const Scalar by,
    const Scalar cx, const Scalar cy)
  {
    const bool all_nonneg = (o_ab >= 0 && o_bc >= 0 && o_ca >= 0);
    const bool all_nonpos = (o_ab <= 0 && o_bc <= 0 && o_ca <= 0);
    if(!(all_nonneg || all_nonpos))
      return false;

    // Fully collinear (or coincident) triple: triangle degenerates to a segment/point.
    if(o_ab == 0 && o_bc == 0 && o_ca == 0)
    {
      const Scalar xmin = std::min(ax, std::min(bx, cx));
      const Scalar xmax = std::max(ax, std::max(bx, cx));
      const Scalar ymin = std::min(ay, std::min(by, cy));
      const Scalar ymax = std::max(ay, std::max(by, cy));
      const Scalar sx = xmax - xmin;
      const Scalar sy = ymax - ymin;

      if(sx == 0.0 && sy == 0.0)
      {
        return (qx == ax) && (qy == ay);
      }

      if(sx >= sy)
      {
        return between_1d(qx, xmin, xmax);
      }
      else
      {
        return between_1d(qy, ymin, ymax);
      }
    }

    return true;
  }

};
}

template <
  typename Derivedq,
  typename Deriveda,
  typename Derivedb,
  typename Derivedc,
  typename Derivedd>
IGL_INLINE igl::Orientation igl::predicates::point_in_convex_hull(
  const Eigen::MatrixBase<Derivedq> & q,
  const Eigen::MatrixBase<Deriveda> & a,
  const Eigen::MatrixBase<Derivedb> & b,
  const Eigen::MatrixBase<Derivedc> & c,
  const Eigen::MatrixBase<Derivedd> & d)
{
  assert(q.size()==2 && a.size()==2 && b.size()==2 && c.size()==2 && d.size()==2);
  using Scalar = typename Derivedq::Scalar;
  using SH = ::SH<Scalar>;
  using igl::Orientation;

  // Check bounding box
  //
  // This was needed for some example where q and (a,b,c,d) were all effectively
  // collinear but not being detected as such due to numerical issues.
  {
    const Scalar min_x = std::min({a(0),b(0),c(0),d(0)});
    const Scalar max_x = std::max({a(0),b(0),c(0),d(0)});
    const Scalar min_y = std::min({a(1),b(1),c(1),d(1)});
    const Scalar max_y = std::max({a(1),b(1),c(1),d(1)});
    if(q(0) < min_x || q(0) > max_x || q(1) < min_y || q(1) > max_y)
    {
      return Orientation::NEGATIVE;
    }
  }

  // For cheap coordinate comparisons (between tests)
  const Scalar qx = (Scalar)q(0), qy = (Scalar)q(1);
  const Scalar px[4] = {(Scalar)a(0), (Scalar)b(0), (Scalar)c(0), (Scalar)d(0)};
  const Scalar py[4] = {(Scalar)a(1), (Scalar)b(1), (Scalar)c(1), (Scalar)d(1)};

  // Cache the 6 unordered edge orientations with q: sign(orient2d(p_i,p_j,q)) for i<j.
  const int s01 = SH::osign(orient2d(a,b,q));
  const int s02 = SH::osign(orient2d(a,c,q));
  const int s03 = SH::osign(orient2d(a,d,q));
  const int s12 = SH::osign(orient2d(b,c,q));
  const int s13 = SH::osign(orient2d(b,d,q));
  const int s23 = SH::osign(orient2d(c,d,q));


  const std::function<int(int,int)> Oq = [&](int i, int j)->int
  {
    // directed sign(orient2d(p_i,p_j,q)) via antisymmetry
    if(i == j) return 0;
    if(i > j) return -Oq(j,i);
    // i<j
    if(i==0 && j==1) return s01;
    if(i==0 && j==2) return s02;
    if(i==0 && j==3) return s03;
    if(i==1 && j==2) return s12;
    if(i==1 && j==3) return s13;
    /*i==2 && j==3*/ return s23;
  };

  auto Pi = [&](int idx)->auto const& {
    switch(idx){
      case 0: return a;
      case 1: return b;
      case 2: return c;
      default:return d;
    }
  };


  // Carathéodory: q ∈ hull(4 pts) iff q ∈ some hull(3 pts)
  auto tri = [&](int ia,int ib,int ic)->bool
  {
    const auto &A = Pi(ia);
    const auto &B = Pi(ib);
    const auto &C = Pi(ic);

    // *** NEW: triangle degeneracy check independent of q ***
    if(orient2d(A,B,C) == Orientation::COLLINEAR)
    {
      // Segment hull of {ia,ib,ic}: pick extremes along dominant axis.
      int ids[3] = {ia,ib,ic};

      // dominant axis based on spread of the three vertices
      Scalar xmin = px[ids[0]], xmax = px[ids[0]];
      Scalar ymin = py[ids[0]], ymax = py[ids[0]];
      for(int t=1;t<3;t++){
        xmin = std::min(xmin, px[ids[t]]); xmax = std::max(xmax, px[ids[t]]);
        ymin = std::min(ymin, py[ids[t]]); ymax = std::max(ymax, py[ids[t]]);
      }
      const bool use_x = (xmax-xmin) >= (ymax-ymin);

      // endpoints are the min/max along that axis
      int u = ids[0], v = ids[0];
      for(int t=1;t<3;t++){
        const int k = ids[t];
        if(use_x){
          if(px[k] < px[u]) u = k;
          if(px[k] > px[v]) v = k;
        }else{
          if(py[k] < py[u]) u = k;
          if(py[k] > py[v]) v = k;
        }
      }

      // all three points coincide
      if(u == v)
        return (qx == px[u]) && (qy == py[u]);

      // q must be collinear with the segment line (use cached orient with q)
      if(Oq(u,v) != 0)
        return false;

      // and between the endpoints
      return SH::on_segment_collinear(qx,qy, px[u],py[u], px[v],py[v]);
    }

    // Non-degenerate triangle: your cached half-plane test is fine.
    const int o_ab = Oq(ia,ib);
    const int o_bc = Oq(ib,ic);
    const int o_ca = Oq(ic,ia);
    return SH::in_triangle_cached(
      o_ab, o_bc, o_ca,
      qx, qy,
      px[ia], py[ia],
      px[ib], py[ib],
      px[ic], py[ic]);
  }; 

  const bool inside =
    tri(0,1,2) || tri(0,1,3) || tri(0,2,3) || tri(1,2,3);

  if(!inside)
    return Orientation::NEGATIVE;

  // --- Boundary classification ---
  // A segment (i,j) is a supporting segment of conv(P) iff the other two points
  // lie in the same closed half-plane of the line through (i,j):
  // sign(orient(p_i,p_j,p_k)) and sign(orient(p_i,p_j,p_l)) are NOT opposite.
  auto supporting_pair = [&](int i, int j)->bool
  {
    int other[2], t=0;
    for(int k=0;k<4;k++) if(k!=i && k!=j) other[t++] = k;

    // Choose Eigen refs for orient2d calls
    auto Pi = [&](int idx)->auto const& {
      switch(idx){
        case 0: return a;
        case 1: return b;
        case 2: return c;
        default:return d;
      }
    };

    const int s1 = SH::osign(orient2d(Pi(i), Pi(j), Pi(other[0])));
    const int s2 = SH::osign(orient2d(Pi(i), Pi(j), Pi(other[1])));

    // Opposite strict signs => not supporting
    return !((s1 > 0 && s2 < 0) || (s1 < 0 && s2 > 0));
  };

  // If hull is fully collinear (degenerate), any in-hull point is "COLLINEAR".
  const bool nondegenerate =
    (orient2d(a,b,c) != Orientation::COLLINEAR) ||
    (orient2d(a,b,d) != Orientation::COLLINEAR) ||
    (orient2d(a,c,d) != Orientation::COLLINEAR) ||
    (orient2d(b,c,d) != Orientation::COLLINEAR);

  if(!nondegenerate)
    return Orientation::COLLINEAR;

  // For non-degenerate hull: q is on boundary iff it lies on some supporting segment.
  for(int i=0;i<4;i++)
  for(int j=i+1;j<4;j++)
  {
    if(!supporting_pair(i,j)) continue;

    if(Oq(i,j) == 0) // q collinear with (p_i, p_j) by exact orient2d
    {
      if(SH::on_segment_collinear(qx,qy, px[i],py[i], px[j],py[j]))
        return Orientation::COLLINEAR;
    }
  }

  return Orientation::POSITIVE; // inside and not on boundary
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template igl::Orientation igl::predicates::point_in_convex_hull<Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&);
template igl::Orientation igl::predicates::point_in_convex_hull<Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>, Eigen::Matrix<double, 1, 2, 1, 1, 2>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, 2, 1, 1, 2>> const&);
template igl::Orientation igl::predicates::point_in_convex_hull<Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>, Eigen::Matrix<double, 1, -1, 1, 1, -1>>(Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&, Eigen::MatrixBase<Eigen::Matrix<double, 1, -1, 1, 1, -1>> const&);
#endif
