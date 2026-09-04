#include "Slic3r/App/Yoga/ScrollArea.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

ScrollArea::ScrollArea(const std::string& name) : Item()
{
    set_object_name(name);
}

void ScrollArea::render(const Vec2f& p, const Vec2f& size)
{
    Vec2f pos = p;

    render_item_begin(pos, size);

    render_debug(pos, size);

    ImGui::SetNextWindowPos(to_im(pos));
    ImGui::SetNextWindowSize(to_im(size));
    ImGui::BeginChild(object_name().c_str(), {size.x(), size.y()}, m_child_flags, m_window_flags);

    // normally ImGui scrolls horizontally with SHIFT + Mouse Wheel, we want to use Mouse Wheel
    // only if m_remap_horizontal_scroll is true
    if (m_remap_horizontal_scroll
        && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        ImGuiIO& io  = ImGui::GetIO();
        float delta  = io.MouseWheel;
        float deltaH = io.MouseWheelH;
        float speed  = ImGui::GetFontSize() * 1.5f;

        float x = ImGui::GetScrollX();
        x -= (delta + deltaH) * speed;
        x = std::clamp(x, 0.0f, ImGui::GetScrollMaxX());
        ImGui::SetScrollX(x);
    }

    if (m_requested_item_scroll
        && index_of(m_requested_item_scroll).has_value()
        && !m_requested_item_scroll->is_node_dirty())
    {
        ImGui::SetScrollX(m_requested_item_scroll->left());
        ImGui::SetScrollY(pos.y() = m_requested_item_scroll->top());

        m_requested_item_scroll = nullptr;
    }

    m_last_scroll = Vec2f{ImGui::GetScrollX(), ImGui::GetScrollY()};
    pos -= m_last_scroll;

    for (Item* child : std::as_const(m_children_render_order)) {
        render_node(pos, child);
    }

    if (!m_children_render_order.empty()) {
        Vec2f content_max = {};
        for (Item* child : m_children_render_order) {
            content_max.x() = std::max(content_max.x(), child->left() + child->width());
            content_max.y() = std::max(content_max.y(), child->top() + child->height());
        }
        Vec2f padding_end = Vec2f(padding().right, padding().bottom);
        ImGui::SetCursorScreenPos(to_im(pos + content_max + padding_end));
    }

    if (Vec2f new_scroll_max = Vec2f{ImGui::GetScrollMaxX(), ImGui::GetScrollMaxY()};
        m_scroll_max != new_scroll_max)
    {
        m_scroll_max = new_scroll_max;
    }

    ImGui::Dummy({});
    ImGui::EndChild();
}

ImGuiChildFlags ScrollArea::child_flags() const
{
    return m_child_flags;
}

void ScrollArea::set_child_flags(ImGuiChildFlags child_flags)
{
    m_child_flags = child_flags;
}

ImGuiWindowFlags ScrollArea::window_flags() const
{
    return m_window_flags;
}

void ScrollArea::set_window_flags(ImGuiWindowFlags window_flags)
{
    m_window_flags = window_flags;
}

bool ScrollArea::remap_horizontal_scroll() const
{
    return m_remap_horizontal_scroll;
}

void ScrollArea::set_remap_horizontal_scroll(bool remap_horizontal_scroll)
{
    m_remap_horizontal_scroll = remap_horizontal_scroll;
}

const Vec2f& ScrollArea::scroll_pos() const
{
    return m_last_scroll;
}

const Vec2f& ScrollArea::scroll_size() const
{
    return m_scroll_max;
}

void ScrollArea::scroll_at_item(Item* item)
{
    m_requested_item_scroll = item;
}

} // namespace Slic3r::App::Yoga
