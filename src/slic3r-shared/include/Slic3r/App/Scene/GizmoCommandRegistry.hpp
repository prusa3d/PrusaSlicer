#pragma once
#include "Slic3r/App/Platform/CommandRegistry.hpp"
#include "Slic3r/App/Scene/IGizmo.hpp"

namespace Slic3r::App::Scene {

class GizmoManager;

/**
 * @brief Gizmo-specific keyboard command registry. It takes care about disabling keyboard commands
 * of inactive tool gizmos.
 */
class GizmoCommandRegistry : public Platform::CommandRegistry
{
public:
    explicit GizmoCommandRegistry(const GizmoManager& gizmo_manager) :
        m_gizmo_manager(gizmo_manager)
    {}

    CommandRegistry& register_command(std::unique_ptr<Platform::ICommand> command) override;
    void set_registering_tool_gizmo(const IToolGizmo& tool_gizmo)
    {
        m_registering_tool_gizmo = &tool_gizmo;
    }

    void reset_registering_tool_gizmo()
    {
        m_registering_tool_gizmo = nullptr;
    }
private:
    const GizmoManager& m_gizmo_manager;
    const IToolGizmo* m_registering_tool_gizmo{nullptr};
};

} // namespace Slic3r::App::Scene
