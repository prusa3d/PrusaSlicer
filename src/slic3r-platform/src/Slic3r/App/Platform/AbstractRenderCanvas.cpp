#include "Slic3r/App/Platform/AbstractRenderCanvas.hpp"

#include <algorithm>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <Slic3r/App/Render/Context.hpp>
#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/CommandBuffer.hpp>
#include <Slic3r/App/Render/Geometry.hpp>
#include <Slic3r/App/Render/Texture.hpp>
#include <Slic3r/App/Render/ScopedDebugGroup.hpp>

#include <tracy/Tracy.hpp>

#include <GL/glew.h>
#include <tracy/TracyOpenGL.hpp>
#include <Slic3r/Log.hpp>


#ifdef NDEBUG
#define assert_no_gl_error()
#else
#define assert_no_gl_error() { GLenum err = glGetError(); assert(err == GL_NO_ERROR);}
#endif

namespace Slic3r::App::Platform {


namespace {
size_t button_to_index(MouseButton button)
{
    switch (button) {
    case MouseButton::Left:
        return 0;
    case MouseButton::Right:
        return 1;
    case MouseButton::Middle:
        return 2;
    case MouseButton::NoButton:
        break;
    }
    return size_t(-1);
}
}

void AbstractRenderCanvas::set_render_module(AbstractRenderModule* render_module)
{
    if (m_render_module == render_module)
        return;

    std::optional<CameraSynchData> camera_data;
    if (m_render_module) {
        m_render_module->deactivate();
        camera_data = m_render_module->camera_synch_data();
    }
    m_render_module = render_module;
    if (m_render_module) {
        m_render_module->set_screen_size(m_screen_info);
        m_render_module->activate(this);
        if (camera_data.has_value())
            m_render_module->set_camera_synch_data(*camera_data);
    }
}

void AbstractRenderCanvas::set_next_render_module(AbstractRenderModule* render_module)
{
    m_next_render_module = render_module;

    /* Ensure the next render module is initialized here before the next rendering pass.
     * Otherwise, for example, PreviewRenderModule::m_viewer wouldn't be initialized
     * and couldn't correctly handle send_data_to_viewer().
     * */
    m_next_render_module->ensure_initialized(device(), imgui_render(), m_animation_manager);
}

void AbstractRenderCanvas::set_screen_size(const Render::ScreenInfo& screen_info)
{
    m_screen_info = screen_info;
    if (m_render_module)
        m_render_module->set_screen_size(m_screen_info);
}

void AbstractRenderCanvas::render()
{
    if (m_render_module == nullptr)
        return;

    TracyGpuZone("GPU: Main frame");

    m_render_module->ensure_initialized(device(), imgui_render(), m_animation_manager);
    if (m_animation_manager.update())
        request_render();

    std::unique_ptr<Render::CommandBuffer> cmd_buffer = device().create_command_buffer();

    assert_no_gl_error();
    {
        Render::ScopedDebugGroup event_new_frame("AbstractRenderCanvas", *cmd_buffer);
        if (!begin_frame()) {
            return;
        }
        assert_no_gl_error();
        emit_enqueued_events();
        assert_no_gl_error();
        begin_imgui_frame();
        assert_no_gl_error();
        m_render_module->render_imgui(*cmd_buffer);
        assert_no_gl_error();
        end_imgui_frame();
        assert_no_gl_error();
        m_render_module->render_scene(*cmd_buffer);
        assert_no_gl_error();
        end_frame(*cmd_buffer);
    }

    if (m_next_render_module) {
        set_render_module(m_next_render_module);
        m_next_render_module = nullptr;
    }

    if (m_animation_manager.is_running()) {
        request_render();
    }
}

Render::ImguiRender& AbstractRenderCanvas::imgui_render()
{
    ZoneScoped;

    if (!m_imgui_render)
        m_imgui_render = std::make_unique<Render::ImguiRender>(device());
    return *m_imgui_render;
}

AbstractRenderCanvas::AbstractRenderCanvas() :
    m_main_thread_dispatcher{Biz::Platform::PlatformServices::instance().main_thread_dispatcher()}
{}

bool AbstractRenderCanvas::begin_frame()
{
    ZoneScoped;

    if (!begin_frame_platform()) {
        return false;
    }
    assert_no_gl_error();

    ImGuiIO& io = ImGui::GetIO();

    double current_time = platform_time();
    io.DeltaTime        = m_last_time > 0 ? float(current_time - m_last_time) : (1.0f / 60.0f);
    m_last_time         = current_time;

    /*
    assert_no_gl_error();
    glViewport(0, 0, m_screen_info.physical_width(), m_screen_info.physical_height());
    assert_no_gl_error();
    // TODO: this should be render module responsibility
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    glClearColor(
        clear_color.x * clear_color.w, clear_color.y * clear_color.w,
        clear_color.z * clear_color.w, clear_color.w
    );
    assert_no_gl_error();
    glClear(GL_COLOR_BUFFER_BIT);
    assert_no_gl_error();
    */
    return true;
}

void AbstractRenderCanvas::begin_imgui_frame()
{
    ZoneScoped;

    // Start the Dear ImGui frame
    m_imgui_render->new_frame();
    begin_imgui_frame_platform();
    ImGui::NewFrame();
}

void AbstractRenderCanvas::end_imgui_frame()
{
    ZoneScoped;

    end_imgui_frame_platform();
    // Rendering
    ImGui::Render();

}

void AbstractRenderCanvas::end_frame(Render::CommandBuffer& cmd_buffer)
{
    ZoneScoped;

    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data) {
        Render::ScopedDebugGroup event_imgui_render("ImGui", cmd_buffer);
        auto& dev = Render::Context::instance().device();
        dev.load_state();
        m_imgui_render->render(cmd_buffer, draw_data);
        cmd_buffer.submit();
    }
    end_frame_platform();
}

void AbstractRenderCanvas::update_key_modifiers(KeyboardEvent::Type event_type, KeyCode code)
{
    KeyModifiers mods{KeyModifiers(KeyModifier::None)};

    if (is_alt(code)) {
        mods |= KeyModifiers(KeyModifier::Alt);
    }
    if (is_shift(code)) {
        mods |= KeyModifiers(KeyModifier::Shift);
    }
    if (is_ctrl(code)) {
        mods |= KeyModifiers(KeyModifier::Ctrl);
    }
    if (is_meta(code)) {
        mods |= KeyModifiers(KeyModifier::Meta);
    }

    if (event_type == KeyboardEvent::Type::KeyUp) {
        m_key_modifiers = m_key_modifiers & ~mods;
    } else if (event_type == KeyboardEvent::Type::KeyDown) {
        m_key_modifiers = m_key_modifiers | mods;
    }
}

void AbstractRenderCanvas::update_mouse_position(int x, int y)
{
    m_mouse_x = x;
    m_mouse_y = y;
}

void AbstractRenderCanvas::enqueue_mouse(const MouseEvent& e)
{
    const auto type = e.type();
    // For double-clicks, wxWidgets generates the following event sequence:
    // Down -> Up -> DblClick -> Up.
    // Therefore, process MouseEvent::Type::DoubleClick as a mouse button press as well.
    if (type == MouseEvent::Type::ButtonDown || type == MouseEvent::Type::DoubleClick) {
        const size_t idx = button_to_index(e.button());

        if (idx < m_mouse_button_pressed.size()) {
            m_mouse_button_pressed[idx] = true;
        }

    } else if (type == MouseEvent::Type::ButtonUp) {
        const size_t idx = button_to_index(e.button());
        if (idx < m_mouse_button_pressed.size()) {
            auto& pressed = m_mouse_button_pressed[idx];
            if (!pressed) {
                SPDLOG_DEBUG(
                    "Missing corresponding button-down event for incoming button-up event "
                    "(button index: {}), skipping button-up event",
                    idx
                );
                return;
            }
            pressed = false;
        }
    }
    m_enqueued_mouse_events.push_back(e);
}
void AbstractRenderCanvas::enqueue_keyboard(const KeyboardEvent& e)
{
    m_enqueued_keyboard_events.push_back(e);
}

void AbstractRenderCanvas::emit_mouse(const MouseEvent& e)
{
    ZoneScoped;

    if (m_render_module)
        m_render_module->on_scene_mouse_event(e);
}

void AbstractRenderCanvas::emit_keyboard(const KeyboardEvent& e)
{
    ZoneScoped;
    if (m_render_module)
        m_render_module->on_scene_keyboard_event(e);
}

void AbstractRenderCanvas::emit_enqueued_events()
{
    ZoneScoped;

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput) {
        for (const auto& e : m_enqueued_keyboard_events)
            emit_keyboard(e);
    }
    m_enqueued_keyboard_events.clear();

    if (io.WantCaptureMouse) {
        for (auto& e : m_enqueued_mouse_events)
            e.set_imgui_captured(true);
    }
    for (const auto& e : m_enqueued_mouse_events)
        emit_mouse(e);

    if (io.WantCaptureMouse
        && std::any_of(io.MouseDown, io.MouseDown + 5, [](bool val) { return val; }))
    {
        request_render();
    }
    m_enqueued_mouse_events.clear();
}


void AbstractRenderCanvas::request_render()
{
    ZoneScoped;

    m_render_request_count = std::max<size_t>(m_render_request_count, 2);
    on_render_requested();
}

bool AbstractRenderCanvas::get_and_reset_render_requested()
{
    if (m_render_request_count > 0) {
        m_render_request_count--;
        return true;
    }
    return false;
}

} // namespace Slic3r::App::Platform
