#pragma once

#include <vector>
#include "Slic3r/Assert.hpp"

#include "Slic3r/App/Platform/AbstractRenderModule.hpp"
#include "Slic3r/Biz/Platform/IRenderRequestHandler.hpp"
#include "Slic3r/Biz/Platform/IMainWindowHandler.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/App/Platform/AnimationManager.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"

#include <Slic3r/App/Render/ImguiRender.hpp>


#ifndef DEBUG_RENDER_TIMING
#define DEBUG_RENDER_TIMING 0
#endif //DEBUG_RENDER_TIMING

namespace Slic3r::App::Render {
class Device;
class CommandBuffer;
}

namespace Slic3r::App::Platform {

/**
 * Abstract class for platform-specific render canvas.
 *
 * Key responsibilities:
 * - facilitate rendering of render module
 * - translate platform specific events and push them the render module
 */
class AbstractRenderCanvas : public Biz::Platform::IRenderRequestHandler, Biz::Platform::IMainWindowHandler
{
public:
    AbstractRenderCanvas();

    ~AbstractRenderCanvas() override = default;

    virtual void render();
    void set_render_module(AbstractRenderModule* render_module);
    void set_next_render_module(AbstractRenderModule* render_module);
    AbstractRenderModule* get_render_module() { return m_render_module; }

    // IRenderRequestHandler interface impl
    void request_render() override;

    /// Default fullscreen handling.
    /// Platforms without native fullscreen support may ignore these calls.
    bool has_fullscreen() const override { return false; }
    bool is_fullscreen() const override { return false; }
    void set_fullscreen(bool on) override {}
    void close_application() override {}

protected:
    /**
     * @brief Do platform specific stuff at the frame beginning
     * @return Return true if the rendering should continue.
     */
    virtual bool begin_frame_platform()       = 0;
    virtual void begin_imgui_frame_platform() = 0;
    virtual void end_imgui_frame_platform() = 0;
    virtual void end_frame_platform() = 0;
    virtual double platform_time() = 0;
    virtual Render::Device& device() = 0;

    /**
     * @brief Called after `request_render` is processed.
     *
     * Empty callback subclass can override to trigger rendering.
     */
    virtual void on_render_requested() {}

    void enqueue_mouse(const MouseEvent& e);
    void enqueue_keyboard(const KeyboardEvent& e);

    virtual void emit_mouse(const MouseEvent& e);

    virtual void emit_keyboard(const KeyboardEvent& e);

    void update_key_modifiers(KeyboardEvent::Type event_type, KeyCode code);
    void update_mouse_position(int x, int y);

    bool get_and_reset_render_requested();

private:
    bool begin_frame();
    void begin_imgui_frame();
    void end_imgui_frame();
    void end_frame(Render::CommandBuffer& cmd_buffer);
    void emit_enqueued_events();

    Render::ImguiRender& imgui_render();

protected:
    void set_screen_size(const Render::ScreenInfo& screen_info);

protected:
    using MouseEvents = std::vector<MouseEvent>;
    using KeyboardEvents = std::vector<KeyboardEvent>;

    AbstractRenderModule* m_render_module       {nullptr};
    AbstractRenderModule* m_next_render_module  {nullptr};
    KeyModifiers m_key_modifiers{KeyModifiers(KeyModifier::None)};

    int m_mouse_x {0};
    int m_mouse_y {0};

    Render::ScreenInfo m_screen_info{0,0,1};

    MouseEvents m_enqueued_mouse_events;
    KeyboardEvents m_enqueued_keyboard_events;
    Biz::Platform::IMainThreadDispatcher& m_main_thread_dispatcher;
    std::array<bool, 3> m_mouse_button_pressed{{false, false, false}};
    size_t m_render_request_count{0};

private:
    std::unique_ptr<Render::ImguiRender> m_imgui_render;
    AnimationManager m_animation_manager;
    double m_last_time{0};
};

} // namespace Slic3r::App::Platform
