#pragma once

#include <functional>
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Scene/Selection.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Biz {
    class ProjectInteractor;
}

namespace Slic3r::App::Yoga {
    class RadioButton;
}

namespace Slic3r::App::Plater {

class ReferenceFramePicker : public Yoga::Item, public Biz::Scene::ISceneSelectionChangedListener
{
public:
    ReferenceFramePicker(
        Biz::ProjectInteractor& project_interactor,
        Biz::Scene::SelectionReferenceFrame preferred_frame
    );

    ~ReferenceFramePicker();

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection&
    ) override;

    void on_scene_selection_transformed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection&
    ) override;

    void on_activated();

    void on_deactivated();
private:
    void reload(std::optional<Domain::SelectionId> project_id = std::nullopt);
    Biz::Scene::SelectionReferenceFrame get_checked_frame() const;

    std::function<void()> m_on_change;
    Biz::ProjectInteractor& m_project_interactor;

    Yoga::ButtonGroup m_mode_buttons;
    Yoga::RadioButton* m_bed_radio_button;
    Yoga::RadioButton* m_instance_radio_button;
    Yoga::RadioButton* m_volume_radio_button;

    struct ProjectContext {
        bool activated{false};
    };

    using ProjectContexts = Biz::ProjectScoped<ProjectContext>;
    ProjectContexts m_projects;
    Biz::Scene::SelectionReferenceFrame m_preferred_frame{
        Biz::Scene::SelectionReferenceFrame::Volume
    };
    bool m_reloading = false;
};
} // namespace Slic3r::App::Plater
