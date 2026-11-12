#pragma once
#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/App/Scene/SceneChangeSession.hpp"

namespace Slic3r::App::Scene {

class ISceneProvider {
public:
    virtual ~ISceneProvider() = default;

    virtual Scene& scene() = 0;
    virtual const Scene& scene() const = 0;
    virtual SceneChangeSession& selection_scene_changes() = 0;
    virtual Node& selection_root() = 0;
    virtual Node& plain_selection_root() = 0; // No scaling, no screen modifier.
};

} // namespace Slic3r::App::Scene
