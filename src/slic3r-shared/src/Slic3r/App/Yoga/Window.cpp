#include "Slic3r/App/Yoga/Window.hpp"

#include "Slic3r/Utils.hpp"

namespace Slic3r::App::Yoga {

Window::Window(const std::string& window_name) : Item(), m_alpha(GImGui->Style.Alpha)
{
    set_object_name(window_name.empty() ? "Window" : window_name);
    set_padding(20_fpx);
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
    return m_alpha;
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

    ImGui::SetNextWindowSize(sz);
    ImGui::SetNextWindowBgAlpha(m_alpha);
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
    if (!window_begun) {
        render_item_end(pos, size);
        return;
    }

    ImVec2 pos_to_render = ImGui::GetWindowPos();

    if (!m_position_by_yoga) {
        ImVec2 xy  = pos_to_render;
        m_last_pos = from_im(xy);
        ImVec2 sz  = ImGui::GetWindowSize();
        ImVec2 max = ImGui::GetMainViewport()->Size - sz;
        ImVec2 pos = {std::clamp(xy.x, 0.f, max.x), std::clamp(xy.y, 0.f, max.y)};
        ImGui::SetWindowPos(pos);
    }

    Vec2f window_pos = from_im(pos_to_render);

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

} // namespace Slic3r::App::Yoga
