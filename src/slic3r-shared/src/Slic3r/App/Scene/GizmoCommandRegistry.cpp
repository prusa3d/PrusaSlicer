#include "Slic3r/App/Scene/GizmoCommandRegistry.hpp"

#include "Slic3r/App/Scene/GizmoManager.hpp"

namespace Slic3r::App::Scene {

class GizmoCommand : public Platform::ICommand
{
public:
    GizmoCommand(
        const GizmoManager& gizmo_manager,
        const IToolGizmo& tool_gizmo,
        std::unique_ptr<Platform::ICommand>&& command
    ) :
        m_gizmo_manager(gizmo_manager),
        m_tool_gizmo(tool_gizmo),
        m_wrapped_command(std::move(command))
    {}

    const char* name() const override
    {
        return m_wrapped_command->name();
    }

    const std::optional<Platform::KeyboardShortcuts> keyboard_shortcuts() const override
    {
        return m_wrapped_command->keyboard_shortcuts();
    }

    bool enabled() const override
    {
        if (!m_gizmo_manager.is_tool_active_in_current_project(m_tool_gizmo))
            return false;
        return m_wrapped_command->enabled();
    }

private:
    void do_execute() const override
    {
        m_wrapped_command->execute();
    }

private:
    const GizmoManager& m_gizmo_manager;
    const IToolGizmo& m_tool_gizmo;
    std::unique_ptr<Platform::ICommand> m_wrapped_command;
};

Platform::CommandRegistry& GizmoCommandRegistry::register_command(
    std::unique_ptr<Platform::ICommand> command
)
{
    CommandRegistry::register_command(
        m_registering_tool_gizmo != nullptr ?
            std::make_unique<GizmoCommand>(m_gizmo_manager, *m_registering_tool_gizmo, std::move(command)) :
            std::forward<std::unique_ptr<Platform::ICommand>>(command)
    );
    return *this;
}

} // namespace Slic3r::App::Scene
