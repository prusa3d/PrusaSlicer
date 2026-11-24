#pragma once

#include <memory>
#include <chrono>
#include <optional>

#include <GL/glew.h>
#include <wx/glcanvas.h>

#include "Slic3r/App/Platform/AbstractRenderCanvas.hpp"

namespace Slic3r::App::Platform::WX {

class WXRenderCanvas : public Platform::AbstractRenderCanvas, public wxGLCanvas
{
public:
    WXRenderCanvas(wxWindow* parent, int id);
    ~WXRenderCanvas();

    WXRenderCanvas(const WXRenderCanvas&)           = delete;
    WXRenderCanvas operator=(const WXRenderCanvas&) = delete;

    void render() override;
    void dispatch_on_main_thread(Biz::Platform::IMainThreadDispatcher::Function func);

    std::unique_ptr<wxGLContext> release_context();

    bool has_fullscreen() const override;
    bool is_fullscreen() const override;
    void set_fullscreen(bool on) override;
    void close_application() override;

protected:
    void on_render_requested() override;

    bool begin_frame_platform() override;
    void begin_imgui_frame_platform() override;
    void end_imgui_frame_platform() override;
    void end_frame_platform() override;
    double platform_time() override;
    Render::Device& device() override;

private:
    void on_paint(wxPaintEvent& event);
    void on_size(wxSizeEvent& event);
    void on_keyboard(wxKeyEvent& evt);
    void on_mouse(wxMouseEvent& event);
    void on_mouse_enter(wxMouseEvent& event);
    void on_mouse_leave(wxMouseEvent& event);
    void on_idle(wxIdleEvent& event);

    static KeyModifiers modifiers(const wxKeyboardState& event);

    /**
     * @brief to_dip is helper to unify ToDIP conversion across OS
     */
    int to_dip(int coord);

    void init();
    void init_wx_imgui();
    void repaint();

private:
    using Clock = std::chrono::high_resolution_clock;
    std::chrono::time_point<Clock> m_start_time;

    std::unique_ptr<wxGLContext> m_gl_context_uniq; ///< Will get released
    wxGLContext* m_gl_context{nullptr}; ///< For subsequent rendering

    bool m_initialized{false};
    bool m_in_render{false};
    bool m_pending_frame{false};
    bool m_presented_once{false};
    bool m_surface_ready{false};
    std::optional<std::chrono::steady_clock::time_point> m_first_present_deadline;

    static constexpr size_t MAX_INFLIGHT_FRAMES{1};
    GLsync m_frame_fence[MAX_INFLIGHT_FRAMES] = {nullptr};
    size_t m_current_frame_idx{0};

    size_t m_timeout_fps{30};

#if DEBUG_RENDER_TIMING
    struct FrameTiming
    {
        double request_time;
        double render_start_time;
        double render_end_time;
    };

    std::optional<FrameTiming> m_current_frame_timing;
    std::vector<FrameTiming> m_frame_timings;
#endif
};

} // namespace Slic3r::App::Platform::WX
