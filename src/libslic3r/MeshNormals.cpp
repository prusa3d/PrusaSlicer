///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros, Lukáš Hejl @hejllukas
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MeshNormals.hpp"

#include <boost/container/small_vector.hpp>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstddef>

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/Execution/Execution.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

static bool point_on_edge(const Vec3d& p, const Vec3d& e1, const Vec3d& e2,
                          double epsSq = 0.05)
{
    using Line3D = Eigen::ParametrizedLine<double, 3>;

    auto line = Line3D::Through(e1, e2);
    return line.squaredDistance(p) < epsSq;
}

Vec3d get_normal(const AABBMesh        &mesh,
                 const Vec3d           &picking_point,
                 double                 eps)
{
    Vec3d ret = Vec3d::Zero();

    int   faceid = 0;
    Vec3d p;

    mesh.squared_distance(picking_point, faceid, p);
    assert(int(faceid) < int(mesh.get_triangle_mesh()->indices.size()));

    auto trindex = mesh.indices(faceid);

    const Vec3d &p1 = mesh.vertices(trindex(0)).cast<double>();
    const Vec3d &p2 = mesh.vertices(trindex(1)).cast<double>();
    const Vec3d &p3 = mesh.vertices(trindex(2)).cast<double>();

    // We should check if the point lies on an edge of the hosting
    // triangle. If it does then all the other triangles using the
    // same two points have to be searched and the final normal should
    // be some kind of aggregation of the participating triangle
    // normals. We should also consider the cases where the support
    // point lies right on a vertex of its triangle. The procedure is
    // the same, get the neighbor triangles and calculate an average
    // normal.

    // Mark the vertex indices of the edge. ia and ib marks an edge.
    // ic will mark a single vertex.
    int vertex_idx = -1;
    int edge_idx = -1;
    double epsSq = eps * eps;
    if ((p - p1).squaredNorm() < epsSq) {
        vertex_idx = trindex(0);
    } else if ((p - p2).squaredNorm() < epsSq) {
        vertex_idx = trindex(1);
    } else if ((p - p3).squaredNorm() < epsSq) {
        vertex_idx = trindex(2);
    } else if (point_on_edge(p, p1, p2, epsSq)) {
        edge_idx = 0;
    } else if (point_on_edge(p, p2, p3, epsSq)) {
        edge_idx = 1;
    } else if (point_on_edge(p, p1, p3, epsSq)) {
        edge_idx = 2;
    }

    // vector for the neigboring triangles including the detected one.
    constexpr size_t MAX_EXPECTED_NEIGHBORS = 10;
    boost::container::small_vector<Vec3d, MAX_EXPECTED_NEIGHBORS> neigh;

    auto &vfidx = mesh.vertex_face_index();
    auto cmpfn = [](const Vec3d &v1, const Vec3d &v2) { return v1.sum() < v2.sum(); };
    auto eqfn = [](const Vec3d &n1, const Vec3d &n2) {
        // Compare normals for equivalence.
        // This is controvers stuff.
        auto deq = [](double a, double b) {
            return std::abs(a - b) < 1e-3;
        };
        return deq(n1(X), n2(X)) &&
               deq(n1(Y), n2(Y)) &&
               deq(n1(Z), n2(Z));
    };

    if (vertex_idx >= 0) { // The point is right on a vertex of the triangle
        neigh.reserve(vfidx.count(vertex_idx));

        auto from = vfidx.begin(vertex_idx);
        auto to   = vfidx.end(vertex_idx);
        for (auto it = from; it != to; ++it) {
            Vec3d nrm = mesh.normal_by_face_id(*it);
            auto oit = std::lower_bound(neigh.begin(), neigh.end(), nrm, cmpfn);
            if (oit == neigh.end() || !eqfn(*oit, nrm))
                neigh.insert(oit, mesh.normal_by_face_id(*it));
        }
    } else if (edge_idx >= 0) { // the point is on and edge
        size_t neighbor_face = mesh.face_neighbor_index()[faceid](edge_idx);
        if (neighbor_face < mesh.indices().size()) {
            neigh.emplace_back(mesh.normal_by_face_id(faceid));
            neigh.emplace_back(mesh.normal_by_face_id(neighbor_face));
        }
    }

    if (!neigh.empty()) { // there were neighbors to count with
        // sum up the normals and then normalize the result again.
        // This unification seems to be enough.
        Vec3d sumnorm(0, 0, 0);
        sumnorm = std::accumulate(neigh.begin(), neigh.end(), sumnorm);
        sumnorm.normalize();
        ret = sumnorm;
    } else { // point lies safely within its triangle
        Eigen::Vector3d U   = p2 - p1;
        Eigen::Vector3d V   = p3 - p1;
        ret = U.cross(V).normalized();
    }

    return ret;
}

template<class Ex>
Eigen::MatrixXd normals(Ex                           ex_policy,
                        const PointSet              &points,
                        const AABBMesh              &mesh,
                        double                       eps,
                        std::function<void()>        thr, // throw on cancel
                        const std::vector<unsigned> &pt_indices)
{
    if (points.rows() == 0 || mesh.vertices().empty() ||
        mesh.indices().empty())
        return {};

    std::vector<unsigned> range = pt_indices;
    if (range.empty()) {
        range.resize(size_t(points.rows()), 0);
        std::iota(range.begin(), range.end(), 0);
    }

    PointSet ret(range.size(), 3);


    execution::for_each(ex_policy, size_t(0), range.size(),
                        [&ret, &mesh, &points, thr, eps, &range](size_t ridx) {
                            thr();
                            unsigned el            = range[ridx];
                            auto     eidx          = Eigen::Index(el);
                            auto     picking_point = points.row(eidx);

                            ret.row(ridx) = get_normal(mesh, picking_point, eps);
                        });

    return ret;
}

template Eigen::MatrixXd normals(ExecutionSeq                 policy,
                                 const PointSet              &points,
                                 const AABBMesh              &convert_mesh,
                                 double                       eps,
                                 std::function<void()>        throw_on_cancel,
                                 const std::vector<unsigned> &selected_points);

template Eigen::MatrixXd normals(ExecutionTBB                 policy,
                                 const PointSet              &points,
                                 const AABBMesh              &convert_mesh,
                                 double                       eps,
                                 std::function<void()>        throw_on_cancel,
                                 const std::vector<unsigned> &selected_points);

} // namespace Slic3r
