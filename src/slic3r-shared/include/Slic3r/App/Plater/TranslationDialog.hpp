#pragma once

#include "Slic3r/App/Plater/ReferenceFramePicker.hpp"
#include "Slic3r/App/Plater/TripleInput.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"

namespace Slic3r::Biz {
    class ProjectInteractor;
}

namespace Slic3r::App::Yoga {
    class InputTextWithSpin;
    class DoubleValidator;
    class RadioButton;
    class ToggleButton;
    class LayoutButton;
}

namespace Slic3r::App::Plater {

class TranslationDialog final :
    public GizmoWindow,
    public App::Plater::ISelectionExtentsChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener
{
public:
    using PositinUpdatedCallback = std::function<void(const Domain::Vec3d&)>;

    TranslationDialog(
        App::Plater::PlaterScenePresenter& scene_provider,
        Biz::ProjectInteractor& project_interactor
    );

    ~TranslationDialog();

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection&
    ) override;

    void on_scene_selection_bounding_box_changed(
        Domain::SelectionId project_id,
        const std::optional<Biz::Scene::SelectionExtents>&
    ) override;

    void on_selected_project_changed_final(size_t index) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override;

    void on_activated();
    void on_deactivated();

private:
    Yoga::ButtonGroup m_mode_buttons;
    ReferenceFramePicker* m_reference_frame_picker;
    TripleInput* m_absolute_input{nullptr};
    Yoga::Item* m_absolute_input_row{nullptr};
    TripleInput* m_relative_input{nullptr};
    Yoga::Item* m_relative_input_row{nullptr};
    App::Plater::PlaterScenePresenter& m_scene_provider;
    Biz::ProjectInteractor& m_project_interactor;

    struct ProjectContext {
        bool activated{false};
    };

    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;
    ProjectContexts m_projects;

    void reload(Domain::SelectionId project_id);

    std::optional<Domain::Vec3d> get_absolute_position_reference_point() const;

    void apply_relative_translation(const Domain::Vec3d& translation);
};
} // namespace Slic3r::App::Plater
