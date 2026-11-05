#include "Slic3r/App/Yoga/Window.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/Utils.hpp"

#include <imgui_internal.h>

#include <cfloat>
#include <cmath>

namespace Slic3r::App::Yoga {

namespace {

float size_limit(const YGValue& value, float fallback)
{
    return value.unit == YGUnitPoint && !std::isnan(value.value) ? value.value : fallback;
}

bool is_resize_handle(ImGuiWindow* window, ImGuiID id)
{
    if (id == 0) {
        return false;
    }
    if (id == ImGui::GetWindowResizeCornerID(window, 0)
        || id == ImGui::GetWindowResizeCornerID(window, 1))
    {
        return true;
    }
    for (int dir = 0; dir < ImGuiDir_COUNT; ++dir) {
        if (id == ImGui::GetWindowResizeBorderID(window, static_cast<ImGuiDir>(dir))) {
            return true;
        }
    }
    return false;
}

ImVec2 viewport_size_limit(
    ImGuiWindow* window,
    const ImVec2& pos,
    const ImVec2& size,
    const ImVec2& min_size,
    const ImVec2& max_size
)
{
    const ImVec2 viewport   = ImGui::GetMainViewport()->Size;
    const ImGuiID active_id = ImGui::GetActiveID();
    const bool left_held    = window
        && (window->ResizeBorderHeld == ImGuiDir_Left
            || active_id == ImGui::GetWindowResizeCornerID(window, 1));
    const bool top_held = window && window->ResizeBorderHeld == ImGuiDir_Up;
    const ImVec2 limit{
        left_held ? pos.x + size.x : viewport.x - pos.x,
        top_held ? pos.y + size.y : viewport.y - pos.y
    };
    return {
        std::max(min_size.x, std::min(max_size.x, limit.x)),
        std::max(min_size.y, std::min(max_size.y, limit.y))
    };
}

bool is_user_resizing(ImGuiWindow* window)
{
    return window->ResizeBorderHeld != -1 || is_resize_handle(window, ImGui::GetActiveID());
}

bool is_resize_handle_pressed(ImGuiWindow* window)
{
    const ImGuiContext* context = ImGui::GetCurrentContext();
    return is_resize_handle(window, context->ActiveId)
        || is_resize_handle(window, context->HoveredIdPreviousFrame);
}

Platform::ColorGroup resize_grip_color_group(ImGuiWindow* window)
{
    const ImGuiID grip_id = ImGui::GetWindowResizeCornerID(window, 0);
    if (ImGui::GetActiveID() == grip_id) {
        return Platform::ColorGroup::Active;
    }
    if (ImGui::GetHoveredID() == grip_id) {
        return Platform::ColorGroup::Hovered;
    }
    return Platform::ColorGroup::Default;
}

} // namespace

Window::Window(const std::string& window_name) : Item()
{
    set_object_name(window_name.empty() ? "Window" : window_name);
    set_padding(20_fpx);

    m_resize_grip = emplace_back<Icon>(Render::Icon::DotsTriangle);
    m_resize_grip->set_position_type(YGPositionTypeAbsolute);
    m_resize_grip->set_right(2_fpx);
    m_resize_grip->set_bottom(2_fpx);
    m_resize_grip->set_width(10_fpx);
    m_resize_grip->set_height(10_fpx);
    m_resize_grip->set_z(-1);
    m_resize_grip->set_visible(false);
}

float Window::rounding() const
{
    return m_rounding;
}

void Window::set_rounding(float new_rounding)
{
    if (!Domain::fuzzy_compare(m_rounding, new_rounding)) {
        m_rounding = new_rounding;
        set_style_dirty();
    }
}

int Window::flags() const
{
    return m_flags;
}

void Window::set_flags(int flags)
{
    if (m_flags != flags) {
        m_flags = flags;
        set_style_dirty();
    }
}

float Window::alpha() const
{
    return m_is_transparent ? m_theme->transparent_window_alpha() : m_alpha;
}

void Window::set_alpha(float alpha)
{
    if (!Domain::fuzzy_compare(m_alpha, alpha)) {
        m_alpha = alpha;
        set_style_dirty();
    }
}

float Window::border_size() const
{
    return m_border_size;
}

void Window::set_border_size(float border_size)
{
    if (!Domain::fuzzy_compare(m_border_size, border_size)) {
        m_border_size = border_size;
        set_style_dirty();
    }
}

void Window::set_border_color(const std::optional<ImColor>& color)
{
    m_border_color = color;
    set_style_dirty();
}

void Window::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    ImVec2 next_pos;
    if (m_position_by_yoga || !m_requested_position.has_value()) {
        next_pos = to_im(pos);
    } else if (m_requested_position.has_value()) {
        next_pos             = to_im(m_requested_position.value());
        m_requested_position = {};
    }

    ImVec2 sz = to_im(size);
    if (!m_position_by_yoga) {
        // clamp
        ImVec2 vp  = ImGui::GetMainViewport()->Size;
        ImVec2 max = ImVec2(vp.x - sz.x, vp.y - sz.y);
        next_pos.x = std::clamp(next_pos.x, 0.f, std::max(0.f, max.x));
        next_pos.y = std::clamp(next_pos.y, 0.f, std::max(0.f, max.y));
    }

    ImGuiWindow* existing_window = ImGui::FindWindowByName(object_name().c_str());
    const bool resizable = !(m_flags & ImGuiWindowFlags_NoResize) && m_callbacks.geometry_changed;
    m_resize_grip->set_visible(resizable);
    if (resizable) {
        ImGui::SetNextWindowSizeConstraints(
            m_min_size_constraint,
            viewport_size_limit(
                existing_window,
                next_pos,
                sz,
                m_min_size_constraint,
                m_max_size_constraint
            )
        );
    }

    ImGui::SetNextWindowSize(sz);
    ImGui::SetNextWindowBgAlpha(alpha());
    ImGui::SetNextWindowPos(next_pos);

    // Discard current paddings and spacing of the window to corect apply of sizer's margins
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, m_border_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));

    bool has_border_color = m_border_color.has_value() && m_border_size > 0.f;
    if (has_border_color) {
        ImGui::PushStyleColor(ImGuiCol_Border, m_border_color.value().Value);
    }

    ScopeGuard style_guard(
        [has_border_color]()
        {
            // Revert current paddings and spacing
            ImGui::PopStyleVar(4);
            if (has_border_color) {
                ImGui::PopStyleColor();
            }
        }
    );

    if (resizable) {
        constexpr ImVec4 transparent{0.f, 0.f, 0.f, 0.f};
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip, transparent);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, transparent);
        ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, transparent);
    }

    // ImGui auto-fits the window to its content on a resize handle double click,
    // which fights the Yoga layout, so the double click is hidden from Begin()
    ImGuiIO& io = ImGui::GetIO();
    const bool suppress_double_click = resizable && io.MouseDoubleClicked[0] && existing_window
        && is_resize_handle_pressed(existing_window);
    if (suppress_double_click) {
        io.MouseDoubleClicked[0] = false;
    }

    bool window_begun{false};
    if (m_is_modal) {
        bool popup_modal_open{true};
        ImGui::OpenPopup(object_name().c_str());
        window_begun = ImGui::BeginPopupModal(object_name().c_str(), &popup_modal_open, m_flags);
        // Our render loop doesn't run continuously, so ImGui's time-based dim
        // fade-in animation only gets a single frame most of the time. Skip it,
        // otherwise the background dims further on the next unrelated render
        // (e.g. mouse moving over the canvas), which looks like a visible jump.
        ImGui::GetCurrentContext()->DimBgRatio = 1.0f;
    } else {
        window_begun = ImGui::Begin(object_name().c_str(), nullptr, m_flags);
    }
    if (resizable) {
        ImGui::PopStyleColor(3);
    }
    if (suppress_double_click) {
        io.MouseDoubleClicked[0] = true;
    }
    if (!window_begun) {
        render_item_end(pos, size);
        return;
    }

    ImVec2 pos_to_render = ImGui::GetWindowPos();

    if (resizable && is_user_resizing(ImGui::GetCurrentWindow())) {
        const ImVec2 actual_size = ImGui::GetWindowSize();
        if (pos_to_render != ImTrunc(next_pos) || actual_size != ImTrunc(sz)) {
            m_callbacks.geometry_changed(from_im(pos_to_render), from_im(actual_size));
        }
    }

    if (!m_position_by_yoga) {
        ImVec2 xy  = pos_to_render;
        m_last_pos = from_im(xy);
        ImVec2 sz  = ImGui::GetWindowSize();
        ImVec2 max = ImGui::GetMainViewport()->Size - sz;
        ImVec2 pos = {std::clamp(xy.x, 0.f, max.x), std::clamp(xy.y, 0.f, max.y)};
        ImGui::SetWindowPos(pos);
    }

    Vec2f window_pos = from_im(pos_to_render);

    if (resizable) {
        m_resize_grip->set_tint(m_theme->color_imgui(
            Platform::Color::ResizeGrip,
            resize_grip_color_group(ImGui::GetCurrentWindow())
        ));
    }

    render_body(window_pos, size);

    render_item_end(window_pos, size);

    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (m_hovered != hovered) {
        m_hovered = hovered;
        if (m_callbacks.hovered_changed) {
            m_callbacks.hovered_changed(m_hovered);
        }
    }

    if (m_requested_bring_to_front) {
        m_requested_bring_to_front = false;
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }

    if (m_is_modal) {
        ImGui::EndPopup();
    } else {
        ImGui::End();
    }
}

void Window::render_body(const Vec2f& pos, const Vec2f& size) {}

void Window::size_limits_changed()
{
    m_max_size_constraint = {
        size_limit(YGNodeStyleGetMaxWidth(node()), FLT_MAX),
        size_limit(YGNodeStyleGetMaxHeight(node()), FLT_MAX)
    };
    const Vec2f min_size = evaluated_min_size();
    m_min_size_constraint = {
        std::clamp(std::isnan(min_size.x()) ? 0.f : min_size.x(), 0.f, m_max_size_constraint.x),
        std::clamp(std::isnan(min_size.y()) ? 0.f : min_size.y(), 0.f, m_max_size_constraint.y)
    };
    YGNodeStyleSetMinWidth(node(), m_min_size_constraint.x);
    YGNodeStyleSetMinHeight(node(), m_min_size_constraint.y);
}

bool Window::is_in_window() const
{
    return true;
}

Window::Callbacks& Window::callbacks()
{
    return m_callbacks;
}

bool Window::position_by_yoga() const
{
    return m_position_by_yoga;
}

void Window::set_position_by_yoga(bool position_by_yoga)
{
    m_position_by_yoga = position_by_yoga;
}

void Window::request_position(const Vec2f& position)
{
    m_requested_position = position;
}

void Window::bring_to_front()
{
    m_requested_bring_to_front = true;
    set_style_dirty();
}

bool Window::hovered() const
{
    return m_hovered;
}

bool Window::is_modal() const
{
    return m_is_modal;
}

void Window::set_modal(bool is_modal)
{
    m_is_modal = is_modal;
}

bool Window::is_transparent() const
{
    return m_is_transparent;
}

void Window::set_transparent(bool is_transparent)
{
    m_is_transparent = is_transparent;
}

} // namespace Slic3r::App::Yoga
