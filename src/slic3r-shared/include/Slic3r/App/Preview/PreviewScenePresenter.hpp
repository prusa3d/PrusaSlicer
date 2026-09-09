#pragma once

#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/App/Scene/ScenePresenterBase.hpp"
#include "Slic3r/App/Scene/ScenePresenterProjectContext.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/App/Preview/PreviewSceneRenderCustomizer.hpp"
#include "Slic3r/App/Scene/Camera.hpp"

namespace Slic3r::App::Platform {
class AnimationManager;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Preview {

class PreviewScenePresenter : public Biz::ISelectedProjectChangedListener,
                              public PreviewSceneRenderCustomizer,
                              public Scene::ScenePresenterBase<Scene::ScenePresenterProjectContext>,
                              public Scene::ICameraUpdateListener
{
public:
    PreviewScenePresenter(
        const Domain::Workbench& m_workbench,
        Biz::ProjectInteractor& project_interactor,
        Render::Device& device,
        Platform::AnimationManager& animation_manager
    );

    void render_scene(Render::CommandBuffer& command_buffer);
    void render_imgui(const Render::ScreenInfo& screen_info);

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

    void remove_all_bed_instances();
    void add_bed_instances(const Domain::BedRefs& instances);
    void update_bed_instances();
    bool update_bed_instance_error_state(const Domain::SlicingId& id, bool error);

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
    bool m_shells_visible{ false };
};

} // namespace Slic3r::App::Preview
