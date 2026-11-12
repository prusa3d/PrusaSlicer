#include "Slic3r/Biz/Algorithms/TriangleSelectorPainter.hpp"

#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"

#include <memory>
#include <utility>
#include <vector>

#include <igl/Hit.h>

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::TriangleSelector::TriangleSplittingData;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

namespace Slic3r::Biz::Algorithms {

TriangleSelectorPainter::TriangleSelectorPainter(
    const Domain::TriangleMesh& mesh,
    const Transform3d& mesh_transform
) :
    m_mesh(mesh),
    m_mesh_transform(mesh_transform),
    m_mesh_transform_no_translate(mesh_transform),
    m_inv_mesh_transform(mesh_transform.inverse().cast<float>()),

    m_selector(mesh),
    m_triangles_tree(
        AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
            mesh.its.vertices,
            mesh.its.indices
        )
    )
{
    m_mesh_transform_no_translate.translation() = Vec3d::Zero();
}

void TriangleSelectorPainter::paint_spot(
    const Vec3f& position,
    const float radius,
    const TriangleStateType state_type
)
{
    const constexpr float eps_angle = 89.99f;
    const float edge_limit          = radius / 5.f;

    const Vec3f point  = m_inv_mesh_transform * position;
    const Vec3f origin = m_inv_mesh_transform * Vec3f(position.x(), position.y(), 0.f);

    std::vector<igl::Hit> hits;
    const Vec3f dir = (point - origin).normalized();
    if (AABBTreeIndirect::intersect_ray_all_hits(
            m_mesh.its.vertices,
            m_mesh.its.indices,
            m_triangles_tree,
            Vec3d(origin.cast<double>()),
            Vec3d(dir.cast<double>()),
            hits
        ))
    {
        for (int hit_idx = static_cast<int>(hits.size()) - 1; hit_idx >= 0; --hit_idx) {
            const igl::Hit& hit = hits[hit_idx];
            const Vec3f pos     = origin + dir * hit.t;

            const Vec3f face_normal = TriangleMesh::its_face_normal(m_mesh.its, hit.id);
            if ((point - pos).norm() < radius && face_normal.dot(dir) < 0) {
                std::unique_ptr<TriangleSelector::Cursor> cursor =
                    std::make_unique<TriangleSelector::Sphere>(
                        pos,
                        origin,
                        radius,
                        m_mesh_transform,
                        TriangleSelector::ClippingPlane{},
                        edge_limit
                    );
                m_selector.select_patch(
                    hit.id,
                    std::move(cursor),
                    state_type,
                    m_mesh_transform_no_translate,
                    true,
                    eps_angle
                );
                break;
            }
        }
    } else {
        size_t hit_idx_out;
        Vec3f hit_point_out;
        const float dist = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
            m_mesh.its.vertices,
            m_mesh.its.indices,
            m_triangles_tree,
            point,
            hit_idx_out,
            hit_point_out
        );
        if (dist < radius) {
            std::unique_ptr<TriangleSelector::Cursor> cursor =
                std::make_unique<TriangleSelector::Sphere>(
                    point,
                    origin,
                    radius,
                    m_mesh_transform,
                    TriangleSelector::ClippingPlane{},
                    edge_limit
                );
            m_selector.select_patch(
                static_cast<int>(hit_idx_out),
                std::move(cursor),
                state_type,
                m_mesh_transform_no_translate,
                true,
                eps_angle
            );
        }
    }
}

TriangleSplittingData TriangleSelectorPainter::serialize() const
{
    return m_selector.serialize();
}

} // namespace Slic3r::Biz::Algorithms
