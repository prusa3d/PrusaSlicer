#pragma once

#include "Slic3r/App/Scene/IRaycastNodeComponent.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/App/Render/Types.hpp"
#include "Slic3r/Domain/Types.hpp"
#include <Slic3r/Biz/Algorithms/AABBMesh.hpp>

namespace Slic3r::App::Scene {

class AabbRaycastNodeComponent : public IRaycastNodeComponent {
public:
    explicit AabbRaycastNodeComponent(const AABBMesh* mesh)
        : m_aabb_mesh(mesh)
    {}

    bool raycast(const Domain::SquareMatrix4d& world, const Ray& ray, RaycastResult& t) const override;
    Eigen::AlignedBox3f world_bounding_box(const Domain::SquareMatrix4d& world) const override;

    AABBMesh::hit_result hit_result(const Domain::SquareMatrix4d& world, const Ray& ray) const;

    bool intersects(const Domain::SquareMatrix4d& world, const Frustum& frustum) const override;
    bool intersects(const Domain::SquareMatrix4d& world, const PickerFrustum& frustum) const override;

    // Eigen::AlignedBox<float, 2> projected_bounding_box(
    //     const Matrix4f& mvp, const Slic3r::App::Render::Rect& viewport
    // ) const override;

private:
    const AABBMesh* m_aabb_mesh{nullptr};
};

}
