#pragma once

#include "Slic3r/App/Scene/Node.hpp"

namespace Slic3r::Biz::Scene {
class SceneInteractor;
} // namespace Slic3r::Biz::Scene

namespace Slic3r::App::Plater {

class SelectionHandler
{
public:
    explicit SelectionHandler(Biz::Scene::SceneInteractor& scene_interactor) :
        m_scene_interactor(scene_interactor)
    {}

    void mark_selected(
        Scene::Node& n,
        bool replace                = true,
        bool dragging               = false,
        bool force_volume_selection = false
    );
    void mark_unselected(Scene::Node& n, bool force_volume_mode = false);
    void clear_selection();

private:
    Biz::Scene::SceneInteractor& m_scene_interactor;
};

} // namespace Slic3r::App::Plater
