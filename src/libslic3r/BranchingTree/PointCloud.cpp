///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PointCloud.hpp"

#include <igl/random_points_on_mesh.h> // IWYU pragma: keep
#include <array>
#include <limits>

#include "libslic3r/Tesselate.hpp"
#include "libslic3r/SLA/SupportTreeUtils.hpp"
#include "admesh/stl.h"
#include "libslic3r/BranchingTree/BranchingTree.hpp"
#include "libslic3r/SLA/Pad.hpp"

namespace Slic3r { namespace branchingtree {

std::optional<Vec3f> find_merge_pt(const Vec3f &A, const Vec3f &B, float max_slope)
{
    return sla::find_merge_pt(A, B, max_slope);
}

void to_eigen_mesh(const indexed_triangle_set &its,
                   Eigen::MatrixXd            &V,
                   Eigen::MatrixXi            &F)
{
    V.resize(its.vertices.size(), 3);
    F.resize(its.indices.size(), 3);
    for (unsigned int i = 0; i < its.indices.size(); ++i)
        F.row(i) = its.indices[i];

    for (unsigned int i = 0; i < its.vertices.size(); ++i)
        V.row(i) = its.vertices[i].cast<double>();
}

std::vector<Node> sample_mesh(const indexed_triangle_set &its, double radius)
{
    std::vector<Node> ret;

    double surface_area = 0.;
    for (const Vec3i &face : its.indices) {
        std::array<Vec3f, 3> tri = {its.vertices[face(0)],
                                    its.vertices[face(1)],
                                    its.vertices[face(2)]};

        auto U = tri[1] - tri[0], V = tri[2] - tri[0];
        surface_area += 0.5 * U.cross(V).norm();
    }

    int N = surface_area / (PI * radius * radius);

    Eigen::MatrixXd B;
    Eigen::MatrixXi FI;
    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    to_eigen_mesh(its, V, F);
    igl::random_points_on_mesh(N, V, F, B, FI);

    ret.reserve(size_t(N));
    for (int i = 0; i < FI.size(); i++) {
        int face_id = FI(i);

        if (face_id < 0 || face_id >= int(its.indices.size()))
            continue;

        Vec3i face = its.indices[face_id];

        if (face(0) >= int(its.vertices.size()) ||
            face(1) >= int(its.vertices.size()) ||
            face(2) >= int(its.vertices.size()))
            continue;

        Vec3f c = B.row(i)(0) * its.vertices[face(0)] +
                  B.row(i)(1) * its.vertices[face(1)] +
                  B.row(i)(2) * its.vertices[face(2)];

        ret.emplace_back(c);
    }

    return ret;
}

std::vector<Node> sample_bed(const ExPolygons &bed, float z, double radius)
{
    auto triangles = triangulate_expolygons_3d(bed, z);
    indexed_triangle_set its;
    its.vertices.reserve(triangles.size());

    for (size_t i = 0; i < triangles.size(); i += 3) {
        its.vertices.emplace_back(triangles[i].cast<float>());
        its.vertices.emplace_back(triangles[i + 1].cast<float>());
        its.vertices.emplace_back(triangles[i + 2].cast<float>());

        its.indices.emplace_back(i, i + 1, i + 2);
    }

    return sample_mesh(its, radius);
}

PointCloud::PointCloud(const indexed_triangle_set &M,
                       std::vector<Node>           support_leafs,
                       const Properties           &props)
    : PointCloud{sample_mesh(M, props.sampling_radius()),
                 sample_bed(props.bed_shape(),
                            props.ground_level(),
                            props.sampling_radius()),
                 std::move(support_leafs), props}
{}

PointCloud::PointCloud(std::vector<Node> meshpts,
                       std::vector<Node> bedpts,
                       std::vector<Node> support_leafs,
                       const Properties &props)
    : m_leafs{std::move(support_leafs)}
    , m_meshpoints{std::move(meshpts)}
    , m_bedpoints{std::move(bedpts)}
    , m_props{props}
    , cos2bridge_slope{std::cos(props.max_slope()) *
                       std::abs(std::cos(props.max_slope()))}
    , MESHPTS_BEGIN{m_bedpoints.size()}
    , LEAFS_BEGIN{MESHPTS_BEGIN + m_meshpoints.size()}
    , JUNCTIONS_BEGIN{LEAFS_BEGIN + m_leafs.size()}
    , m_searchable_indices(JUNCTIONS_BEGIN + m_junctions.size(), true)
    , m_queue_indices(JUNCTIONS_BEGIN + m_junctions.size(), Unqueued)
    , m_reachable_cnt{JUNCTIONS_BEGIN + m_junctions.size()}
{
    for (size_t i = 0; i < m_bedpoints.size(); ++i) {
        m_bedpoints[i].id = int(i);
        m_ktree.insert({m_bedpoints[i].pos, i});
    }
    
    for (size_t i = 0; i < m_meshpoints.size(); ++i) {
        Node &n = m_meshpoints[i];
        n.id = int(MESHPTS_BEGIN + i);
        m_ktree.insert({n.pos, n.id});
    }
    
    for (size_t i = 0; i < m_leafs.size(); ++i) {
        Node &n = m_leafs[i];
        n.id    = int(LEAFS_BEGIN + i);
        n.left  = Node::ID_NONE;
        n.right = Node::ID_NONE;

        m_ktree.insert({n.pos, n.id});
    }
}

float PointCloud::get_distance(const Vec3f &p, size_t node_id) const
{
    auto t = get_type(node_id);
    auto ret = std::numeric_limits<float>::infinity();
    const auto &node = get(node_id);
    
    switch (t) {
    case MESH:
    case BED: {
        // Points of mesh or bed which are outside of the support cone of
        // 'pos' must be discarded.
        if (is_outside_support_cone(p, node.pos))
            ret = std::numeric_limits<float>::infinity();
        else
            ret  = (node.pos - p).norm();
        
        break;
    }
    case LEAF:
    case JUNCTION:{
        auto mergept = find_merge_pt(p, node.pos, m_props.max_slope());
        double maxL2 = m_props.max_branch_length() * m_props.max_branch_length();

        if (!mergept || mergept->z() < (m_props.ground_level() + 2 * node.Rmin))
            ret = std::numeric_limits<float>::infinity();
        else if (double a = (node.pos - *mergept).squaredNorm(),
                 b        = (p - *mergept).squaredNorm();
                 a < maxL2 && b < maxL2)
            ret = std::sqrt(b);

        break;
    }
    case NONE:
        ;
    }
    
    // Setting the ret val to infinity will effectively discard this
    // connection of nodes. max_branch_length property is used here
    // to discard node=>node and node=>mesh connections longer than this
    // property.
    if (t != BED && ret > m_props.max_branch_length())
        ret = std::numeric_limits<float>::infinity();
    
    return ret;
}

}} // namespace Slic3r::branchingtree
