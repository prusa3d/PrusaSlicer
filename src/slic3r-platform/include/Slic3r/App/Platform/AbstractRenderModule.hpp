#pragma once

#include "Slic3r/App/Platform/MouseEvent.hpp"
#include "Slic3r/App/Platform/KeyboardEvent.hpp"
#include "Slic3r/Biz/Platform/IRenderRequestHandler.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/App/Platform/CommandRegistry.hpp"
#include "Slic3r/App/Platform/CommandName.hpp"
#include "Slic3r/App/Platform/CameraSynchData.hpp"

namespace Slic3r::Domain {
enum class PrinterTechnology : uint8_t;
} // namespace Slic3r::Domain

namespace Slic3r::App {
class Navigator;
class MenuManager;
class CommandBindingManager;

namespace Scene {
class IToolGizmo;
enum class ToolType : uint8_t;
} // namespace Scene

} // namespace Slic3r::App

namespace Slic3r::App::Platform {
class AnimationManager;
} // namespace Slic3r::App::Platform

namespace Slic3r::App::Render {
class Device;
class CommandBuffer;
class ImguiRender;
} // namespace Slic3r::App::Render

namespace Slic3r::App::Platform {

/**
 * Provides abstract interface for render module and common infrastructure for rendering
 * and event processing.
 */
class AbstractRenderModule
{
public:
    virtual ~AbstractRenderModule() = default;

    virtual void render_scene(Render::CommandBuffer& cmd_buffer) = 0;
    virtual void render_imgui(Render::CommandBuffer& cmd_buffer) = 0;

    virtual void on_scene_mouse_event(const MouseEvent& e);
    virtual void on_scene_keyboard_event(const KeyboardEvent& e);

    void activate(Biz::Platform::IRenderRequestHandler* render_request_handler);
    void deactivate();

    void set_screen_size(const Render::ScreenInfo& screen_info);
    void ensure_initialized(
        Render::Device& device,
        Render::ImguiRender& imgui_render,
        AnimationManager& animation_manager
    );
    bool is_initialized() const;

    virtual const std::optional<CameraSynchData>& camera_synch_data() const = 0;
    virtual void set_camera_synch_data(const CameraSynchData& data)         = 0;

    virtual void set_sidebars_visible(bool visible) {};

    virtual const ICommand& command(const char* name) const;
    bool has_command(const char* name) const;
    const CommandRegistry::CommandsMap& commands() const;
    virtual const CommandRegistry::CommandsMap& gizmo_commands() const = 0;
    virtual bool is_gizmo_manager_completed() const;
    virtual CommandBindingManager& command_binding_manager() = 0;
    virtual MenuManager& menu_manager()                      = 0;
    virtual Scene::IToolGizmo* tool_gizmo(Scene::ToolType, Domain::PrinterTechnology)
    {
        return nullptr;
    }

    virtual void get_user_number_and_process(
        const std::string& message,
        const std::string& prompt,
        const std::string& title,
        int value,
        int min,
        int max,
        std::function<void(int)> on_process
    ) {}

protected:
    /**
     * Initialize all Render objects here.
     */
    virtual void on_init(
        Render::Device& device,
        Render::ImguiRender& imgui_render,
        Platform::AnimationManager& animation_manager
    );

    virtual void on_activated();
    virtual void on_deactivated();
    virtual void on_screen_resized();

    virtual void register_commands();

    virtual void bind_commands();

    void request_render();

    virtual void set_navigator(Navigator* n) = 0;

protected:
    Render::Device* m_device{nullptr};
    CommandRegistry m_command_registry;
    Render::ImguiRender* m_imgui_render{nullptr};
    AnimationManager* m_animation_manager{nullptr};

    Render::ScreenInfo m_screen_info{0, 0, 1};

private:
    bool m_initialized{false};
    Biz::Platform::IRenderRequestHandler* m_render_request_handler{nullptr};
};

} // namespace Slic3r::App::Platform
