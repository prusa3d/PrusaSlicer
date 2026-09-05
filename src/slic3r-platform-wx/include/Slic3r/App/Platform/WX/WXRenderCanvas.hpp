#pragma once

#include <memory>
#include <chrono>

#include <GL/glew.h>
#include <wx/event.h>
#include <wx/glcanvas.h>

#include "Slic3r/App/Platform/AbstractRenderCanvas.hpp"
#include "Slic3r/App/Platform/MouseEvent.hpp"

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
    void on_gesture_zoom(wxZoomGestureEvent& event);

    /**
     * @brief Translate a wheel event into the three scroll units the app needs.
     *
     * Handles precise (trackpad) and notched devices alike, and accumulates
     * sub-detent movement so that quantised consumers still see whole steps.
     */
    MouseEvent::Scroll build_scroll(const wxMouseEvent& evt);

    /// Feed the camera a zoom as if it came from the wheel, at the given position.
    void emit_synthetic_zoom(float wheel_delta, int x, int y, KeyModifiers mods);

    static KeyModifiers modifiers(const wxKeyboardState& event);

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

    static constexpr size_t MAX_INFLIGHT_FRAMES{1};
    GLsync m_frame_fence[MAX_INFLIGHT_FRAMES] = {nullptr};
    size_t m_current_frame_idx{0};

    size_t m_timeout_fps{30};

    // Sub-detent wheel movement carried over between events, so that a trackpad
    // produces one discrete step per detent-worth of travel rather than one per
    // event. Reset when the scroll direction reverses.
    float m_wheel_residue_x{0};
    float m_wheel_residue_y{0};

    // Zoom factor reported by the last pinch gesture event. wx reports the
    // factor relative to the start of the gesture, so it has to be differenced.
    double m_gesture_last_zoom{1.0};

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
