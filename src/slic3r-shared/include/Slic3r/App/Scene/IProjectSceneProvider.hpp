#pragma once

#include "Slic3r/App/Scene/Scene.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::App::Scene {

class IProjectSceneProvider
{
public:
    virtual ~IProjectSceneProvider() = default;

    virtual Scene& project_scene(Domain::SelectionId project_id) = 0;
    virtual const Scene& project_scene(Domain::SelectionId project_id) const = 0;
};

} // namespace Slic3r::App::Scene
