#pragma once

#include "Slic3r/App/Plater/BedSelectGizmo.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"
#include "Slic3r/Biz/Arrange/Settings.hpp"
#include "Slic3r/App/Plater/ArrangeDialog.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"

namespace Slic3r::App::Scene {
class GeometryDataFactory;
} // namespace Slic3r::App::Scene

namespace Slic3r::Domain {
class Workbench;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {
class ProjectInteractor;
class ArrangeInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {

class ArrangeGizmo final :
    public Scene::IToolGizmo,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::Platform::JobManager::IJobManagerStatusChangedListener,
    public Biz::Slicing::IWipeTowerGeometryListener
{
public:
    ArrangeGizmo(
        Biz::ArrangeInteractor& arrange_interactor,
        Render::Device& device,
        Scene::ISceneProvider& scene_provider,
        Scene::GeometryDataFactory& data_factory,
        Biz::ProjectInteractor& project_interactor,
        const Domain::Workbench& workbench
    );

    ~ArrangeGizmo();

    Scene::GizmoActivationState on_mouse(Scene::GizmoEventContext& ctx, bool only_active) override;

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_wipe_tower_geometry_changed(Biz::Slicing::OptWipeTowerGeometry,
                                        const Domain::SlicingId) override;

    void update_dialog();

    void on_job_manager_status_changed(const Biz::Platform::JobManager::JobManagerStatus& status) override;

    void on_activated() override;
    void on_deactivated() override;

    void register_commands(Platform::CommandRegistry& registry) override;

    Scene::ToolType type() const override;

    bool enabled() const override;

    GizmoWindowPtr release_ui_window() override;

private:
    Biz::ArrangeInteractor& m_arrange_interactor;
    Render::Device& m_device;
    Scene::ISceneProvider& m_scene_provider;
    Scene::GeometryDataFactory& m_data_factory;
    Biz::ProjectInteractor& m_project_interactor;
    const Domain::Workbench& m_workbench;
    Yoga::Passthrough<ArrangeDialog> m_dialog;
    bool m_active{false};

    Biz::Arrange::Settings default_settings() const;
    void arrange_selected_config_container();
    void arrange_selection_in_selected_config_container();
    void arrange_selected_beds();
    void arrange_selection_on_selected_beds();
};
} // namespace Slic3r::App::Plater
