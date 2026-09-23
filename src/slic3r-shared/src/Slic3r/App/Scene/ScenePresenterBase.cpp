#include "Slic3r/App/Scene/ScenePresenterBase.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"
#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenterProjectContext.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include <algorithm>

namespace Slic3r::App::Scene {

template <typename ProjectContextT>
ScenePresenterBase<ProjectContextT>::ScenePresenterBase(
    const Domain::Workbench& workbench,
    Biz::ProjectInteractor& project_interactor,
    Render::Device& device,
    Platform::AnimationManager& animation_manager
) :
    m_workbench(workbench),
    m_project_interactor(project_interactor),
    m_device(device),
    m_animation_manager(animation_manager),
    m_bed_render_updater(*this, workbench, device, project_interactor.scene_interactor())
{
    AppServices::instance().app_config_interactor().add_listener<IAppConfigChangedListener>(this);
    project_interactor.preset_interactor().add_listener<Biz::Preset::IPresetChangedListener>(this);
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::on_node_added(Node* node)
{
    if (node != nullptr && node->contains_raycast_component())
        set_scene_aabb_as_dirty();
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::on_node_removed(Node* node)
{
    if (node != nullptr && node->contains_raycast_component())
        set_scene_aabb_as_dirty();
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::on_node_changed(Node* node)
{
    if (node != nullptr && node->contains_raycast_component())
        set_scene_aabb_as_dirty();
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::screen_resized(const Render::Rect& viewport)
{
    m_viewport = viewport;
    update_cameras([&viewport](auto& cam) { cam.set_viewport(viewport); });
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::center_camera_on_selected_bed(bool animated)
{
    if (animated)
        animated_center_camera_on_bed(m_workbench.project(m_project_interactor.selected_project_id()),
            m_project_interactor.scene_interactor().bed_selection().last_selected_bed(), scene().camera_trackball(),
            m_animation_manager);
    else
        center_camera_on_bed(m_workbench.project(m_project_interactor.selected_project_id()),
            m_project_interactor.scene_interactor().bed_selection().last_selected_bed(), scene().camera_trackball());
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (type == Biz::Preset::PresetItemType::PrinterPreset)
        center_camera_on_selected_bed(true);
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (Biz::Scene::SceneInteractor::is_bed_related_preset_key(item.def().name))
        center_camera_on_selected_bed(true);
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::update_cameras(const std::function<void(Camera&)>& modifier)
{
    std::for_each(m_projects.begin(), m_projects.end(),
        [modifier](auto& p) { modifier(p.second.scene().camera()); });
}

template <typename ProjectContextT>
void ScenePresenterBase<ProjectContextT>::on_app_config_changed(const std::string& key)
{
    if (key == "camera_projection_type") {
        auto type = AppServices::instance().app_config().get<CameraProjectionType>("camera_projection_type");
        update_cameras([type](Camera& cam) { cam.set_projection_type(type); });
    }
}

template class ScenePresenterBase<ScenePresenterProjectContext>;
template class ScenePresenterBase<Plater::PlaterScenePresenterProjectContext>;

} // namespace Slic3r::App::Scene
