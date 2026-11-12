#pragma once

#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/SceneChangeSession.hpp"
#include "Slic3r/App/Render/GeometryManager.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/AuxiliaryElementId.hpp"
#include "Slic3r/App/Scene/BedError.hpp"
#include "Slic3r/App/Platform/CameraSynchData.hpp"
#include "Slic3r/App/Scene/ModelGeometryProvider.hpp"

#define ENABLE_DEBUG_RENDER_SCENE_AABB 0

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Scene {

class ScenePresenterProjectContext
{
public:
    ScenePresenterProjectContext();

    ScenePresenterProjectContext(const ScenePresenterProjectContext&) = delete;
    ScenePresenterProjectContext& operator=(const ScenePresenterProjectContext&) = delete;
    ScenePresenterProjectContext(ScenePresenterProjectContext&&) = default;

    Scene& scene() { return *m_scene; }
    const Scene& scene() const { return *m_scene; }

private: // Intialization order matters, hence this out of order private.
    std::unique_ptr<Scene> m_scene;
    SceneChangeSession m_selection_scene_change_session;

public:
    Node& selection_root;
    Node& plain_selection_root;

    SceneChangeSession& selection_scene_changes()
    {
        return m_selection_scene_change_session;
    }

    const BedError& bed_error() const { return m_bed_error; }
    BedError& bed_error() { return m_bed_error; }

    std::shared_ptr<ModelGeometryProvider> model_geometry_provider();
    void set_model_geometry_provider(std::shared_ptr<ModelGeometryProvider> provider);

    ModelGeometryProvider::GeometryManager& model_geometry_manager();
    const ModelGeometryProvider::GeometryManager& model_geometry_manager() const;
    ModelGeometryProvider::TriangleMeshManager& model_triangle_mesh_manager();
    const ModelGeometryProvider::TriangleMeshManager& model_triangle_mesh_manager() const;


    const std::optional<Platform::CameraSynchData>& camera_synch_data() const { return m_camera_synch_data; }
    void set_camera_synch_data(const Platform::CameraSynchData& data) { m_camera_synch_data = data; }

    double screen_space_sized_modifier() const { return 0.0075; }

#if ENABLE_DEBUG_RENDER_SCENE_AABB
    void set_scene_aabb_node_as_dirty() { m_scene_aabb_node.dirty = true; }
    void update_scene_aabb_node(Render::Device& device, const Eigen::AlignedBox3d& aabb);
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB

private:
    std::shared_ptr<ModelGeometryProvider> m_model_geometry_provider;
    std::optional<Platform::CameraSynchData> m_camera_synch_data;
    BedError m_bed_error;
#if ENABLE_DEBUG_RENDER_SCENE_AABB
    struct SceneAABBNode
    {
        Node* node{ nullptr };
        bool dirty{ true };
    };
    SceneAABBNode m_scene_aabb_node;
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB
};

} // namespace Slic3r::App::Scene
