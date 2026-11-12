#ifndef MESHNORMALS_HPP
#define MESHNORMALS_HPP

#include <Eigen/Geometry>
#include <functional>
#include <vector>

#include "Slic3r/Biz/Algorithms/AABBMesh.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionSeq.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp"

namespace Slic3r {
class AABBMesh;

// Get a good approximation of the normal for any picking point on the mesh.
// For points projecting to a face, this is the face normal, but when the
// picking point is on an edge or a vertex of the mesh, the normal is the
// normalized sum of each unique face normal (works nicely). The eps parameter
// gives a tolerance for how close a sample point has to be to an edge or
// vertex to start considering neighboring faces for the resulting normal.
Domain::Vec3d get_normal(const AABBMesh &mesh,
                 const Domain::Vec3d    &picking_point,
                 double          eps = 0.05);

using PointSet = Eigen::MatrixXd;

// Calculate the normals for the selected points (from 'points' set) on the
// mesh. This will call squared distance for each point.
template<class Ex>
Eigen::MatrixXd normals(
    Ex                           ex_policy,
    const PointSet              &points,
    const AABBMesh              &convert_mesh,
    double                       eps = 0.05, // min distance from edges
    std::function<void()>        throw_on_cancel = []() {},
    const std::vector<unsigned> &selected_points = {});

extern template Eigen::MatrixXd normals(
    Biz::Algorithms::Execution::ExecutionSeq policy,
    const PointSet& points,
    const AABBMesh& convert_mesh,
    double eps,
    std::function<void()> throw_on_cancel,
    const std::vector<unsigned>& selected_points
);

extern template Eigen::MatrixXd normals(
    Biz::Algorithms::Execution::ExecutionTBB policy,
    const PointSet& points,
    const AABBMesh& convert_mesh,
    double eps,
    std::function<void()> throw_on_cancel,
    const std::vector<unsigned>& selected_points
);

} // namespace Slic3r

#endif // MESHNORMALS_HPP
