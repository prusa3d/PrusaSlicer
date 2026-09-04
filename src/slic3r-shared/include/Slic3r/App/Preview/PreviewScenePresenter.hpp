#pragma once

#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/App/Preview/PreviewSceneRenderCustomizer.hpp"
#include "Slic3r/App/Scene/BedRenderUpdater.hpp"
#include "Slic3r/App/Scene/CameraFrustumUpdater.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"

namespace Slic3r::App::Platform {
class AnimationManager;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Preview {

class PreviewScenePresenter : public Biz::ISelectedProjectChangedListener,
                              public PreviewSceneRenderCustomizer,
                              public Scene::ISceneProvider,
                              public Scene::ICameraUpdateListener,
                              public Scene::ISceneChangedListener
{
public:
    using ProjectContexts = std::unordered_map<Domain::SelectionId, Scene::ScenePresenterProjectContext>;

    PreviewScenePresenter(
        const Domain::Workbench& m_workbench,
        Biz::ProjectInteractor& project_interactor,
        Render::Device& device,
        Platform::AnimationManager& animation_manager
    );

    void render_scene(Render::CommandBuffer& command_buffer);
    void render_imgui(const Render::ScreenInfo& screen_info);

    void screen_resized(const Render::Rect& viewport);

    /**
     * @name Implementation of Scene::ISceneProvider public interface
     * @{
     */
    Scene::Scene& scene() override { return project_context().scene(); }
    const Scene::Scene& scene() const override { return project_context().scene(); }
    Scene::SceneChangeSession& selection_scene_changes() override {
        return project_context().selection_scene_changes();
    }
    Scene::Node& selection_root() override {
        return project_context().selection_root;
    }
    Scene::Node& plain_selection_root() override {
        return project_context().plain_selection_root;
    }
    /**@}*/

    /**
     * @name Implementation of Biz::ISelectedProjectChangedListener public interface
     * @{
     */
    void on_selected_project_changed(size_t index) override;
    /**@}*/

    /**
     * @name Implementation of Scene::ICameraUpdateListener public interface
     * @{
     */
    void camera_updated(const Scene::Camera& cam) override { set_scene_aabb_as_dirty(); }
    /**@}*/

    /**
     * @name Implementation of App::Scene::ISceneChangedListener public interface
     * @{
     */
    void on_node_added(Scene::Node* node) override;
    void on_node_removed(Scene::Node* node) override;
    void on_node_changed(Scene::Node* node) override;
    /**@}*/

    void remove_all_bed_instances();
    void add_bed_instances(const Domain::BedRefs& instances);
    void update_bed_instances();
    bool update_bed_instance_error_state(const Domain::SlicingId& id, bool error);

    void center_camera_on_selected_bed(bool animated);

    const std::optional<Platform::CameraSynchData>& camera_synch_data() const { return project_context().camera_synch_data(); }
    void set_camera_synch_data(const Platform::CameraSynchData& data) { project_context().set_camera_synch_data(data); }

    void set_model_geometry_provider(std::shared_ptr<Scene::ModelGeometryProvider> provider)
    {
        return project_context().set_model_geometry_provider(provider);
    }

    void remove_all_shells();
    void add_shells();
    void update_shells_visibility();

    bool are_shells_visible() const { return m_shells_visible; }
    void toggle_shells_visibility()
    {
        m_shells_visible = !m_shells_visible;
        update_shells_visibility();
    }

private:
    Scene::ScenePresenterProjectContext& project_context()
    {
        ASSERT(m_selected_project_id != Domain::INVALID_ID);
        return m_projects[m_selected_project_id];
    }

    const Scene::ScenePresenterProjectContext& project_context() const
    {
        ASSERT(m_selected_project_id != Domain::INVALID_ID);
        return m_projects.find(m_selected_project_id)->second;
    }

    void update_cameras(const std::function<void(Scene::Camera&)>& modifier);

    void set_scene_aabb_as_dirty() { m_camera_frustum_updater.set_scene_aabb_as_dirty(); }

private:
    const Domain::Workbench& m_workbench;
    Biz::ProjectInteractor& m_project_interactor;
    Render::Device& m_device;
    Platform::AnimationManager& m_animation_manager;
    Render::Rect m_viewport;

    Domain::SelectionId m_selected_project_id{ Domain::INVALID_ID };
    ProjectContexts m_projects;
    Scene::BedRenderUpdater m_bed_render_updater;
    Scene::CameraFrustumUpdater m_camera_frustum_updater;
    bool m_shells_visible{ false };
};

} // namespace Slic3r::App::Preview
