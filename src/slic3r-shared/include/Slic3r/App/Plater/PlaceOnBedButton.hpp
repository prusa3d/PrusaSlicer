#pragma once

#include "Slic3r/Domain/SelectionId.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::App::Plater {
class PlaceOnBedButton :
    public Yoga::LayoutButton,
    public Biz::ISelectedProjectChangedListener,
    public Biz::Scene::ISceneSelectionChangedListener
{
public:
    explicit PlaceOnBedButton(Biz::ProjectInteractor& project_interactor);

    void on_selected_project_changed_final(size_t) override;

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection&
    ) override
    {}

    void on_scene_selection_bounding_box_updated(
        Domain::SelectionId,
        const Biz::Scene::ObjectSelection&
    ) override;

protected:
    void action_internal() override;

private:
    void reload();

private:
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;

    Biz::ListenerScope<
        Biz::ISelectedProjectChangedListener,
        Biz::ProjectInteractor,
        PlaceOnBedButton>
        m_selected_project_listener_scope;

    Biz::ListenerScope<
        Biz::Scene::ISceneSelectionChangedListener,
        Biz::Scene::SceneInteractor,
        PlaceOnBedButton>
        m_scene_selection_listener_scope;
};
} // namespace Slic3r::App::Plater
