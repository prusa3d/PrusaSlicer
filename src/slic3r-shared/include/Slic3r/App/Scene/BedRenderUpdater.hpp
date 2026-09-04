#pragma once

#include "Slic3r/App/Scene/ISceneProvider.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/App/Scene/BedError.hpp"

namespace Slic3r::Domain {
class Project;
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::App::Scene {

class BedRenderUpdater :
    public ICameraUpdateListener,
    public Biz::ISelectedProjectChangedListener
{
public:
    BedRenderUpdater(
        ISceneProvider& scene_provider,
        const Domain::Workbench& workbench,
        Render::Device& device,
        const Biz::Scene::SceneInteractor& scene_interactor
    ) :
        m_scene_provider(scene_provider),
        m_workbench(workbench),
        m_device(device),
        m_scene_interactor{scene_interactor}
    {}

    /**
     * @brief Performs all updates
     */
    void update_all(const Camera& cam, const BedError& bed_error)
    {
        update_materials(bed_error);
        update_shadows(cam);
        update_positions();
        update_elements_state();
        camera_updated(cam);
    }

    /**
      * @brief Updates beds' materials in dependence of the scene status
      */
    void update_materials(const BedError& bed_error);

    /**
      * @brief Updates beds' shadows data in dependence of the scene status
      */
    void update_shadows(const Camera& cam);

    /**
      * @brief Updates beds' position in scene
      */
    void update_positions();

    /**
      * @brief Updates beds' elements state
      */
    void update_elements_state();

    /**
      * @brief Implementation of Scene::ICameraUpdateListener interface
      */
    void camera_updated(const Camera& cam) override;

    /**
      * @brief Implementation of Biz::ISelectedProjectChangedListener interface
      */
    void on_selected_project_changed(Domain::SelectionId project_id) override;

private:
    ISceneProvider& m_scene_provider;
    const Domain::Workbench& m_workbench;
    Render::Device& m_device;
    const Domain::Project* m_project{ nullptr };
    Domain::SelectionId m_project_id{Domain::INVALID_ID};
    const Biz::Scene::SceneInteractor& m_scene_interactor;
    BedError m_bed_error;
};

} // namespace Slic3r::App::Scene
