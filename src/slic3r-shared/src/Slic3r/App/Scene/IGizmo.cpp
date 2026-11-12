#include "Slic3r/App/Scene/IGizmo.hpp"

#include "Slic3r/App/Plater/GizmoWindow.hpp"

namespace Slic3r::App::Scene {

bool IToolGizmo::supports_printer(Domain::PrinterTechnology pt) const
{
    return true;
}

bool IToolGizmo::enabled() const
{
    return true;
}

std::unique_ptr<Plater::GizmoWindow> IToolGizmo::release_ui_window()
{
    return nullptr;
}

} // namespace Slic3r::App::Scene
