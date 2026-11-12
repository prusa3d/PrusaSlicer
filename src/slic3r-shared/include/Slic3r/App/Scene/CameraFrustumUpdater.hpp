#pragma once

#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"

namespace Slic3r::App::Scene {

class CameraFrustumUpdater
{
public:
    void set_scene_aabb_as_dirty() { m_scene_aabb.dirty = true; }
#if ENABLE_DEBUG_RENDER_SCENE_AABB
    void update_scene_aabb(ScenePresenterProjectContext& ctx);
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB
    void update_scene_aabb(const Scene& scene);

    void update_camera_frustum(Camera& camera);

#if ENABLE_DEBUG_RENDER_SCENE_AABB
    void update_scene_aabb_node(ScenePresenterProjectContext& ctx, Render::Device& device);
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB

private:
    struct SceneAABB
    {
        Eigen::AlignedBox3d aabb;
        bool dirty{ true };
    };
    SceneAABB m_scene_aabb;
};

} // namespace Slic3r::App::Scene
