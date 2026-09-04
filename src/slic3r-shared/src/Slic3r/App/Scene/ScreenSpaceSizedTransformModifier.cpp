#include "Slic3r/App/Scene/ScreenSpaceSizedTransformModifier.hpp"
#include "Slic3r/App/Scene/Node.hpp"
#include "Slic3r/Log.hpp"
#include <cmath>

namespace Slic3r::App::Scene {

void ScreenSpaceSizedTransformModifier::modify_world_transform(
    Slic3r::App::Scene::Transform& world_xform
)
{
    const auto obj_world_pos = world_xform.matrix().block<3, 1>(0, 3);
    const auto cam_world_pos = m_camera.position();
    const double dist = (obj_world_pos - cam_world_pos).norm();

    const double scale = m_camera.cam_projection()
        .constant_screen_space_size_scale(m_camera, dist) * m_preserved_scale;

    // remove scaling part
    const auto basis_x = world_xform.matrix().block<3, 1>(0, 0).normalized();
    const auto basis_y = world_xform.matrix().block<3, 1>(0, 1).normalized();
    const auto basis_z = world_xform.matrix().block<3, 1>(0, 2).normalized();
    world_xform.matrix().block<3, 1>(0, 0) = basis_x * scale;
    world_xform.matrix().block<3, 1>(0, 1) = basis_y * scale;
    world_xform.matrix().block<3, 1>(0, 2) = basis_z * scale;
}

void ScreenSpaceSizedTransformModifier::camera_updated(const Camera& cam)
{
    // as camera changed => the node world xform will change
    // => marking the node as dirty => the modify_world_transform gets called
    m_node.mark_world_transform_dirty();
}

} // namespace Slic3r::App::Scene
