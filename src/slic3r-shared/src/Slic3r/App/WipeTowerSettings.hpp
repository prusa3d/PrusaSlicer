#pragma once

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/App/Plater/TripleInput.hpp"

namespace Slic3r::App {
class WipeTowerSettings : public Yoga::Item, public Biz::Scene::ISceneChangedListener
{
public:
    WipeTowerSettings(Biz::ProjectInteractor& project_interactor);

    void on_wipe_tower_moved(Domain::SlicingId slicing_id) override;

private:
    void reload();
    const Domain::BedInstance& get_bed_instance() const;
    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    const Domain::Workbench& m_workbench;

    Plater::TripleInput* m_position_input{nullptr};
    Plater::TripleInput* m_rotation_input{nullptr};

    Biz::ListenerScope<
        Biz::Scene::ISceneChangedListener,
        Biz::Scene::SceneInteractor,
        WipeTowerSettings>
        m_scene_changed_listener_scope;
};
} // namespace Slic3r::App
