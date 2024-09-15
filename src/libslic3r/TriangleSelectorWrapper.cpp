///|/ Copyright (c) Prusa Research 2022 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TriangleSelectorWrapper.hpp"

#include <igl/Hit.h>
#include <memory>
#include <utility>
#include <vector>

#include "admesh/stl.h"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

namespace Slic3r {

TriangleSelectorWrapper::TriangleSelectorWrapper(const TriangleMesh &mesh, const Transform3d& mesh_transform) :
        mesh(mesh), mesh_transform(mesh_transform), selector(mesh), triangles_tree(
                AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(mesh.its.vertices, mesh.its.indices)) {
}

void TriangleSelectorWrapper::enforce_spot(const Vec3f &point, const Vec3f &origin, float radius) {
    std::vector<igl::Hit> hits;
    Vec3f dir = (point - origin).normalized();
    static constexpr const auto eps_angle = 89.99f;
    Transform3d trafo_no_translate = mesh_transform;
    trafo_no_translate.translation() = Vec3d::Zero();
    if (AABBTreeIndirect::intersect_ray_all_hits(mesh.its.vertices, mesh.its.indices, triangles_tree,
            Vec3d(origin.cast<double>()),
            Vec3d(dir.cast<double>()),
            hits)) {
        for (int hit_idx = hits.size() - 1; hit_idx >= 0; --hit_idx) {
            const igl::Hit &hit = hits[hit_idx];
            Vec3f pos = origin + dir * hit.t;
            Vec3f face_normal = its_face_normal(mesh.its, hit.id);
            if ((point - pos).norm() < radius && face_normal.dot(dir) < 0) {
                std::unique_ptr<TriangleSelector::Cursor> cursor = std::make_unique<TriangleSelector::Sphere>(
                        pos, origin, radius, this->mesh_transform, TriangleSelector::ClippingPlane { });
                selector.select_patch(hit.id, std::move(cursor), TriangleStateType::ENFORCER, trafo_no_translate,
                        true, eps_angle);
                break;
            }
        }
    } else {
        size_t hit_idx_out;
        Vec3f hit_point_out;
        float dist = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(mesh.its.vertices, mesh.its.indices,
                triangles_tree, point, hit_idx_out, hit_point_out);
        if (dist < radius) {
            std::unique_ptr<TriangleSelector::Cursor> cursor = std::make_unique<TriangleSelector::Sphere>(
                    point, origin, radius, this->mesh_transform, TriangleSelector::ClippingPlane { });
            selector.select_patch(hit_idx_out, std::move(cursor), TriangleStateType::ENFORCER,
                    trafo_no_translate,
                    true, eps_angle);
        }
    }
}

}
