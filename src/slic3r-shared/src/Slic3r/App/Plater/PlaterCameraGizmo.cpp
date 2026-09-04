#include "Slic3r/App/Plater/PlaterCameraGizmo.hpp"
#include "Slic3r/App/Plater/NodePredicates.hpp"

namespace Slic3r::App::Plater {

bool PlaterCameraGizmo ::any_draggable(Scene::GizmoEventContext& ctx) const
{
    return Plater::any_draggable(ctx.pick_results());
}

} // namespace Slic3r::App::Plater