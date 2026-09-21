#pragma once

#include "Slic3r/Assert.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/ISceneChangedListener.hpp"
#include "Slic3r/App/Scene/BedRenderUpdater.hpp"
#include "Slic3r/App/Scene/CameraFrustumUpdater.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"

#include <functional>
#include <optional>
#include <unordered_map>

namespace Slic3r::App::Platform {
class AnimationManager;
struct CameraSynchData;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Scene {

/**
 * @brief Base class shared by the scene presenters (PlaterScenePresenter, PreviewScenePresenter)
 *        that own one Scene per open project and expose it through ISceneProvider.
 *
 * @tparam ProjectContextT Per-project state, must derive from Scene::ScenePresenterProjectContext.
 *         Templated because Plater and Preview each need their own richer project context type,
 *         while sharing everything below in terms of storage and camera/scene bookkeeping.
 */
template <typename ProjectContextT>
class ScenePresenterBase :
    public ISceneProvider,
    public ISceneChangedListener,
    public IAppConfigChangedListener,
    public Biz::Preset::IPresetChangedListener
{
public:
    using ProjectContexts = std::unordered_map<Domain::SelectionId, ProjectContextT>;

    ScenePresenterBase(
        const Domain::Workbench& workbench,
        Biz::ProjectInteractor& project_interactor,
        Render::Device& device,
        Platform::AnimationManager& animation_manager
    );

    /**
     * @name Implementation of Scene::ISceneProvider public interface
     * @{
     */
    Scene& scene() override { return project_context().scene(); }
    const Scene& scene() const override { return project_context().scene(); }
    SceneChangeSession& selection_scene_changes() override { return project_context().selection_scene_changes(); }
    Node& selection_root() override { return project_context().selection_root; }
    Node& plain_selection_root() override { return project_context().plain_selection_root; }
    /**@}*/

    /**
     * @name Implementation of Scene::ISceneChangedListener public interface
     * @{
     */
    void on_node_added(Node* node) override;
    void on_node_removed(Node* node) override;
    void on_node_changed(Node* node) override;
    /**@}*/

    void screen_resized(const Render::Rect& viewport);

    void center_camera_on_selected_bed(bool animated);

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;

    const std::optional<Platform::CameraSynchData>& camera_synch_data() const { return project_context().camera_synch_data(); }
    void set_camera_synch_data(const Platform::CameraSynchData& data) { project_context().set_camera_synch_data(data); }

protected:
    ProjectContextT& project_context()
    {
        ASSERT(m_selected_project_id != Domain::INVALID_ID);
        return m_projects[m_selected_project_id];
    }

    const ProjectContextT& project_context() const
    {
        ASSERT(m_selected_project_id != Domain::INVALID_ID);
        return m_projects.find(m_selected_project_id)->second;
    }

    void update_cameras(const std::function<void(Camera&)>& modifier);

    /**
     * @brief Implementation of IAppConfigChangedListener public interface
     */
    void on_app_config_changed(const std::string& key) override;

    void set_scene_aabb_as_dirty() { m_camera_frustum_updater.set_scene_aabb_as_dirty(); }

protected:
    const Domain::Workbench& m_workbench;
    Biz::ProjectInteractor& m_project_interactor;
    Render::Device& m_device;
    Platform::AnimationManager& m_animation_manager;
    Render::Rect m_viewport;

    Domain::SelectionId m_selected_project_id{ Domain::INVALID_ID };
    ProjectContexts m_projects;
    BedRenderUpdater m_bed_render_updater;
    CameraFrustumUpdater m_camera_frustum_updater;
};

} // namespace Slic3r::App::Scene
