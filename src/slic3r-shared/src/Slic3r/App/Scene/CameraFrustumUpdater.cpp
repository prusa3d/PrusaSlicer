#include "Slic3r/App/Scene/CameraFrustumUpdater.hpp"
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"

namespace Slic3r::App::Scene {

#if ENABLE_DEBUG_RENDER_SCENE_AABB
void CameraFrustumUpdater::update_scene_aabb(ScenePresenterProjectContext& ctx)
{
    update_scene_aabb(ctx.scene());
    ctx.set_scene_aabb_node_as_dirty();
}
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB

void CameraFrustumUpdater::update_scene_aabb(const Scene& scene)
{
    if (!m_scene_aabb.dirty)
        return;

    Node::ConstNodeList nodes;
    scene.root().query([](const Node* n) { return n->has_raycast_component(); }, nodes);

    if (nodes.empty())
        m_scene_aabb.aabb = { Domain::Vec3d{-1., -1.0, -1.0}, Domain::Vec3d{1.0, 1.0, 1.0} };
    else {
        m_scene_aabb.aabb = Eigen::AlignedBox3d();
        for (const Node* n : nodes) {
            m_scene_aabb.aabb.extend(
                n->raycast_component()->world_bounding_box(n->world_transform().matrix()).cast<double>()
            );
        }
    }
    m_scene_aabb.dirty = false;
}

void CameraFrustumUpdater::update_camera_frustum(Camera& camera)
{
    DEBUG_ASSERT(!m_scene_aabb.aabb.isEmpty());
    DEBUG_ASSERT(!m_scene_aabb.dirty);

    Domain::Transform3d view = Domain::Transform3d(camera.view().matrix());
    double z_min             = DBL_MAX;
    double z_max             = -DBL_MAX;
    for (size_t i = 0; i < 8; ++i) {
        Domain::Vec3d v_eye = view * m_scene_aabb.aabb.corner(Eigen::AlignedBox3d::CornerType(i));
        z_min               = std::min(z_min, -v_eye.z());
        z_max               = std::max(z_max, -v_eye.z());
    }
    static constexpr double MARGIN = 1.0;
    camera.set_z_near_far(std::max(10.0, z_min - MARGIN), std::max(1000.0, z_max + MARGIN));
}

#if ENABLE_DEBUG_RENDER_SCENE_AABB
void CameraFrustumUpdater::update_scene_aabb_node(ScenePresenterProjectContext& ctx, Render::Device& device)
{
    DEBUG_ASSERT(!m_scene_aabb.dirty);
    ctx.update_scene_aabb_node(device, m_scene_aabb.aabb);
}
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB

} // namespace Slic3r::App::Scene
