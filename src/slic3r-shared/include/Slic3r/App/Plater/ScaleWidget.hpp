#pragma once

#include "Slic3r/App/Plater/PlaceOnBedButton.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Plater/ReferenceFramePicker.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Plater {
class TripleInput;

class ScaleWidget final :
    public Yoga::Item,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISelectedProjectChangedListener
{
public:
    ScaleWidget(
        Biz::ProjectInteractor& project_interactor,
        Yoga::LayoutButton* revert_button,
        ReferenceFramePicker* reference_frame_picker
    );

    ~ScaleWidget();

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection&
    ) override;

    void on_scene_selection_transformed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection&
    ) override;

    void on_scene_selection_bounding_box_updated(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection&
    ) override;

    void on_selected_project_changed_final(size_t index) override;

    void on_activated(Domain::SelectionId project_id);
    void on_deactivated();

private:
    Biz::ProjectInteractor& m_project_interactor;
    TripleInput* m_absolute_input{nullptr};
    TripleInput* m_absolute_percent_input{nullptr};
    Yoga::LayoutButton* m_revert_button{nullptr};
    Yoga::Item* m_absolute_percent_input_item{nullptr};
    TripleInput* m_relative_input{nullptr};
    Yoga::Item* m_relative_input_item{nullptr};
    Yoga::LayoutButton* m_lock{nullptr};
    PlaceOnBedButton* m_place_on_bed_button{nullptr};
    ReferenceFramePicker* m_reference_frame_picker;

    struct ProjectContext
    {
        bool activated{false};
        Biz::Scene::SceneInteractor::ElementTransforms reset_scale_candidates;
    };

    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;
    ProjectContexts m_projects;

    void reload(std::optional<Domain::SelectionId> project_id = std::nullopt);
    void apply_relative_scale(const Domain::Vec3d& scale);
    void reset_scale();
    Biz::Scene::SceneInteractor::ElementTransforms get_reset_scale_candidates() const;
};
} // namespace Slic3r::App::Plater
