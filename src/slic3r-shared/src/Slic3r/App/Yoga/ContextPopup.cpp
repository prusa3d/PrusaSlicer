#include "Slic3r/App/Yoga/ContextPopup.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

ContextPopup::ContextPopup(const std::string& name)
{
    ItemPtr content = std::make_unique<Item>();
    m_content_item  = content.get();
    m_content_item->set_flex_grow(1);
    Item::insert(std::move(content), 0);
    m_background_color = m_theme->color_imgui(Platform::Color::WindowBg);
    set_object_name(name.empty() ? "ContextMenu" : name);
    set_position_type(YGPositionType::YGPositionTypeAbsolute);
}

void ContextPopup::insert(ObjectPtr child, size_t index)
{
    m_content_item->insert(std::move(child), index);
}

void ContextPopup::append(ObjectPtr child)
{
    m_content_item->append(std::move(child));
}

ObjectPtr ContextPopup::remove(Object* child)
{
    return m_content_item->remove(child);
}

ContextPopup::Callbacks& ContextPopup::callbacks()
{
    return m_callbacks;
}

void ContextPopup::style_node()
{
    if (m_opened && is_visible()) {
        const ImVec2 size(m_size_info.viewport_size_x, m_size_info.viewport_size_y);

        if (m_open_pos.has_value()) {
            // Absolute positioning
            ImRect popup_rect(
                m_open_pos->x(),
                m_open_pos->y(),
                m_open_pos->x() + width(),
                m_open_pos->y() + height()
            );

            Imgui::move_window_to_bounds(size, popup_rect);

            set_left(popup_rect.Min.x);
            set_top(popup_rect.Min.y);
        } else {
            // Relative positioning according to the parent_item and m_position
            const float parent_w = parent_item()->width();
            const float parent_h = parent_item()->height();
            const float popup_w  = width();
            const float popup_h  = height();

            bool is_left = true;
            bool is_top  = true;

            ImVec2 local_pos;

            switch (m_position) {
            case Position::Right:
                is_left = false;
                is_top  = true;

                local_pos.x = parent_w + m_offset;
                local_pos.y =
                    m_flags & ImGuiWindowFlags_ChildMenu ? 0.0f : parent_h * 0.5f - popup_h * 0.5f;
                break;
            case Position::Left:
                is_left = true;
                is_top  = true;

                local_pos.x = -(m_offset + popup_w);
                local_pos.y = parent_h * 0.5f - popup_h * 0.5f;
                break;
            case Position::Top:
                is_left = true;
                is_top  = true;

                local_pos.x = parent_w * 0.5f - popup_w * 0.5f;
                local_pos.y = -(m_offset + popup_h);
                break;
            case Position::Bottom:
                is_left = true;
                is_top  = false;

                local_pos.x =
                    m_flags & ImGuiWindowFlags_ChildMenu ? 0.0f : parent_w * 0.5f - popup_w * 0.5f;
                local_pos.y = parent_h + m_offset;
                break;
            }

            const ImVec2 parent_pos = to_im(parent_item()->get_global_pos());
            ImRect target_rect{
                parent_pos + local_pos,
                parent_pos + local_pos + ImVec2{popup_w, popup_h}
            };
            Imgui::move_window_to_bounds(size, target_rect);

            if (is_left) {
                set_left(target_rect.Min.x - parent_pos.x);
            } else {
                set_right(parent_pos.x + parent_w - target_rect.Max.x);
            }

            if (is_top) {
                set_top(target_rect.Min.y - parent_pos.y);
            } else {
                set_bottom(parent_pos.y + parent_h - target_rect.Max.y);
            }
        }
        set_max_width(size.x);
        set_max_height(size.y);
    }

    Item::style_node();
}

void ContextPopup::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    if (m_force_open_popup_in_render) {
        ImGui::OpenPopup(object_name().c_str());
        m_force_open_popup_in_render = false;
    }

    ImGui::SetNextWindowSize(to_im(size));
    ImGui::SetNextWindowPos(to_im(pos));

    // Discard current paddings and spacing of the window to corect apply of sizer's margins
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, m_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, m_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_background_color.Value);

    bool begin = false;
    if (m_flags & ImGuiWindowFlags_ChildMenu) {
        // Ugly positioning hack, ImGui is forcing position if we are using
        // child menu. Pivot positioning forces our supplied position.
        ImVec2 pivot{1.0f, 1.0f};
        ImVec2 p = to_im(pos);
        ImVec2 s = to_im(size);

        ImGui::SetNextWindowSize(s, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(p.x + s.x, p.y + s.y), ImGuiCond_Always, pivot);

        ImGuiID id = ImGui::GetID(object_name().c_str());
        begin      = ImGui::BeginPopupMenuEx(id, object_name().c_str(), m_flags);
    } else {
        begin = ImGui::BeginPopup(object_name().c_str(), m_flags);
    }
    if (begin) {
        m_opened = true;
        m_content_item->set_visible(true);
        if (m_request_close) {
            ImGui::CloseCurrentPopup();
            m_request_close = false;
        }

        render_item_end(pos, size);

        if (m_flags & ImGuiWindowFlags_ChildMenu) {
            ImGui::EndMenu();
        } else {
            ImGui::EndPopup();
        }
    } else if (m_opened) {
        m_opened = false;
        m_content_item->set_visible(false);
        if (m_callbacks.closed) {
            m_callbacks.closed();
        }
    }

    // Revert current paddings and spacing
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);
}

float ContextPopup::offset() const
{
    return m_offset;
}

void ContextPopup::set_offset(float offset)
{
    if (!Domain::fuzzy_compare(m_offset, offset)) {
        m_offset = offset;
        invalidate_style();
    }
}

std::optional<Vec2f> ContextPopup::open_pos() const
{
    return m_open_pos;
}

void ContextPopup::set_open_pos(std::optional<Vec2f> pos)
{
    if (pos != m_open_pos) {
        m_open_pos = pos;
        invalidate_style();
    }
}

Position ContextPopup::position() const
{
    return m_position;
}

void ContextPopup::set_position(Position position)
{
    if (m_position != position) {
        m_position = position;
        invalidate_style();
    }
}

float ContextPopup::rounding() const
{
    return m_rounding;
}

void ContextPopup::set_rounding(float rounding)
{
    if (!Domain::fuzzy_compare(m_rounding, rounding)) {
        m_rounding = rounding;
        set_style_dirty();
    }
}

void ContextPopup::open()
{
    m_request_close = false;
    if (!m_force_open_popup_in_render) {
        ImGui::OpenPopup(object_name().c_str());
    }
    set_style_dirty();
    if (m_callbacks.opened) {
        m_callbacks.opened();
    }
}

void ContextPopup::open(Vec2f pos)
{
    set_open_pos(pos);
    m_force_open_popup_in_render = true;
    open();
}

void ContextPopup::close()
{
    m_request_close = true;
    set_style_dirty();
}

bool ContextPopup::opened() const
{
    return ImGui::IsPopupOpen(object_name().c_str());
}

ImGuiWindowFlags ContextPopup::flags() const
{
    return m_flags;
}

void ContextPopup::set_flags(ImGuiWindowFlags flags)
{
    m_flags = flags;
}

void ContextPopup::invalidate_style()
{
    set_top(YGUndefined);
    set_bottom(YGUndefined);
    set_left(YGUndefined);
    set_right(YGUndefined);
    set_style_dirty();
}

Item* ContextPopup::content_item() const
{
    return m_content_item;
}

const ImColor& ContextPopup::background_color() const
{
    return m_background_color;
}

void ContextPopup::set_background_color(const ImColor& background_color)
{
    m_background_color = background_color;
}

} // namespace Slic3r::App::Yoga
