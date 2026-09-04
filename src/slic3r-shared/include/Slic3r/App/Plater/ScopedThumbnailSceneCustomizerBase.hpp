#pragma once

#include "Slic3r/App/Scene/GraphicsSettings.hpp"
#include "Slic3r/App/Render/Material.hpp"
#include "Slic3r/App/Scene/CameraFrustumUpdater.hpp"
#include "Slic3r/Domain/Types.hpp"

namespace Slic3r::App::Scene {
class Scene;
class Node;
} // namespace Slic3r::App::Scene

namespace Slic3r::Domain {
class Project;
struct BedRef;
struct BedInstance;
} // namespace Slic3r::Domain

namespace Slic3r::App::Plater {

class ScopedThumbnailSceneCustomizerBase
{
public:
    ScopedThumbnailSceneCustomizerBase(Scene::Scene& scene, const Domain::Project& project, Scene::CameraProjectionType camera_type)
        : m_project(project), m_scene(scene), m_camera_trackball(m_camera)
    {
        // Notify listeners that thumbnail rendering begins.
        m_scene.notify_thumbnail_render_begin();

        if (m_camera.cam_projection().type() != camera_type)
            m_camera.switch_projection_type();
    }
    virtual ~ScopedThumbnailSceneCustomizerBase();

    ScopedThumbnailSceneCustomizerBase(const ScopedThumbnailSceneCustomizerBase& other) = delete;
    ScopedThumbnailSceneCustomizerBase(ScopedThumbnailSceneCustomizerBase&& other) = delete;
    ScopedThumbnailSceneCustomizerBase& operator=(const ScopedThumbnailSceneCustomizerBase& other) = delete;
    ScopedThumbnailSceneCustomizerBase& operator=(ScopedThumbnailSceneCustomizerBase&& other) = delete;

    Scene::Camera& camera() { return m_camera; }
    const Scene::Camera& camera() const { return m_camera; }

protected:
    void store_shading_type();
    void store_background_enabled();
    void store_use_background_error_color();

    void hide_gizmos();
    void hide_selection_aabb();
    void hide_non_part_volumes();
    void hide_non_selected_bed_instances(const Domain::BedRef& selected_bed_instance);
    void hide_volumes_outside_selected_bed_instances(const Domain::BedInstance& bed_instance);
    void hide_bed_accessories();
    void hide_non_printable_volumes();

    void disable_bed_override_material();
    void disable_volumes_override_material();
    void override_non_printable_volumes_material();
    void set_shadows();

    void set_background_enabled(bool enabled);
    void set_use_background_error_color(bool use);
    void set_shading_type(Scene::ShadingType type);
    void set_camera_trackball(const Eigen::AlignedBox3d& aabb);
    void zoom_to_box(const Eigen::AlignedBox3d& aabb);
    void update_camera_frustum();

    Eigen::AlignedBox3d scene_aabb() const;
    Eigen::AlignedBox3d volume_parts_aabb() const;

protected:
    const Domain::Project& m_project;
    Scene::Scene& m_scene;
    Scene::Camera m_camera;
    Scene::CameraTrackballController m_camera_trackball;
    Scene::CameraFrustumUpdater m_camera_frustum_updater;

    struct Cache
    {
        std::vector<Scene::Node*> hidden_nodes;
        std::vector<std::pair<Scene::Node*, std::optional<Render::Material>>> materials;
        std::vector<std::pair<Scene::Node*, Render::Shadows>> shadows;
        bool background_enabled{ true };
        bool use_background_error_color{ false };
        Scene::ShadingType shading_type{ Scene::ShadingType::Legacy };
    };

    Cache m_cache;
};

} // namespace Slic3r::App::Plater
