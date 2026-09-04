#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"
#include "Slic3r/App/Scene/OBBNodeHelper.hpp"

namespace Slic3r::App::Scene {

static Node& initialize_node(const std::string& debug_name, bool constant_screen_size, Scene& scene)
{
    NodeBuilder builder{scene};
    builder.set_debug_name(debug_name);
    if (constant_screen_size) {
        builder.set_screen_space_sized_modifier(SELECTION_ROOT_SCALE_MODIFIER);
    }
    Node* node{builder.build().release()};
    scene.add_child(node);
    return *node;
}

std::shared_ptr<ModelGeometryProvider> ScenePresenterProjectContext::model_geometry_provider()
{
    ASSERT(m_model_geometry_provider != nullptr);
    return m_model_geometry_provider;
}

void ScenePresenterProjectContext::set_model_geometry_provider(std::shared_ptr<ModelGeometryProvider> provider)
{
    ASSERT(provider != nullptr);
    m_model_geometry_provider = provider;
}

ModelGeometryProvider::GeometryManager& ScenePresenterProjectContext::model_geometry_manager()
{
    ASSERT(m_model_geometry_provider != nullptr);
    return m_model_geometry_provider->geometry_manager;
}

const ModelGeometryProvider::GeometryManager& ScenePresenterProjectContext::model_geometry_manager() const
{
    ASSERT(m_model_geometry_provider != nullptr);
    return m_model_geometry_provider->geometry_manager;
}

ModelGeometryProvider::TriangleMeshManager& ScenePresenterProjectContext::model_triangle_mesh_manager()
{
    ASSERT(m_model_geometry_provider != nullptr);
    return m_model_geometry_provider->triangle_mesh_manager;
}

const ModelGeometryProvider::TriangleMeshManager& ScenePresenterProjectContext::model_triangle_mesh_manager() const
{
    ASSERT(m_model_geometry_provider != nullptr);
    return m_model_geometry_provider->triangle_mesh_manager;
}

#if ENABLE_DEBUG_RENDER_SCENE_AABB
void ScenePresenterProjectContext::update_scene_aabb_node(Render::Device& device, const Eigen::AlignedBox3d& aabb)
{
    if (!m_scene_aabb_node.dirty)
        return;

    if (m_scene_aabb_node.node == nullptr) {
        NodeBuilder builder(*m_scene);
        build_obb_node(builder, model_geometry_manager(), device, "scene_aabb", 0, Domain::ColorRGB::YELLOW());
        m_scene_aabb_node.node = builder.build().release();
        m_scene->add_child(m_scene_aabb_node.node);
    }

    DEBUG_ASSERT(m_scene_aabb_node.node != nullptr);
    update_obb_node(*m_scene_aabb_node.node, { aabb.center(), aabb.sizes() });
    m_scene_aabb_node.dirty = false;
}
#endif // ENABLE_DEBUG_RENDER_SCENE_AABB

ScenePresenterProjectContext::ScenePresenterProjectContext() :
    m_scene{std::make_unique<Scene>()},
    m_selection_scene_change_session{*m_scene},
    selection_root{initialize_node("global_selection_root", true, *m_scene)},
    plain_selection_root{initialize_node("scaling_global_selection_root", false, *m_scene)}
{}

} // namespace Slic3r::App::Scene
