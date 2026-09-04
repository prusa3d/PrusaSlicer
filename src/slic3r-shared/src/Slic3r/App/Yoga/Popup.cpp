#include "Slic3r/App/Yoga/Popup.hpp"

#include "Slic3r/App/Yoga/RootItem.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include <Slic3r/Log.hpp>

#include <imgui_internal.h>

#include <list>

namespace Slic3r::App::Yoga {

namespace {

[[nodiscard]] std::pair<bool, ImRect> try_to_place_popup(
    const ImRect& size_rect,
    const ImRect& target_rect,
    const ImVec2& attachee_size,
    float offset,
    Position position,
    AlignU align
)
{
    const float alignment = [&]
    {
        switch (align) {
        case AlignU::Start:
            return 0.f;
        case AlignU::Center:
            return 0.5f;
        case AlignU::End:
            return 1.f;
        }
        return 0.5f;
    }();

    const auto aligned_position = [alignment](float min, float max, float size)
    { return std::lerp(min, max, alignment) - size * alignment; };

    ImVec2 min;
    switch (position) {
    case Position::Left:
        min = {
            target_rect.Min.x - offset - attachee_size.x,
            aligned_position(target_rect.Min.y, target_rect.Max.y, attachee_size.y)
        };
        break;

    case Position::Right:
        min = {
            target_rect.Max.x + offset,
            aligned_position(target_rect.Min.y, target_rect.Max.y, attachee_size.y)
        };
        break;

    case Position::Top:
        min = {
            aligned_position(target_rect.Min.x, target_rect.Max.x, attachee_size.x),
            target_rect.Min.y - offset - attachee_size.y
        };
        break;

    case Position::Bottom:
        min = {
            aligned_position(target_rect.Min.x, target_rect.Max.x, attachee_size.x),
            target_rect.Max.y + offset
        };
        break;
    }

    const ImRect rect{min, min + attachee_size};
    return {size_rect.Contains(rect), rect};
}

} // namespace

Popup::Popup()
{
    set_object_name("Popup");
    m_offset.source = 10;
    m_popup_node    = YGNodeNewWithConfig(m_config);
    YGNodeStyleSetDisplay(m_popup_node, YGDisplayNone);
}

Popup::~Popup()
{
    close();

    YGNodeFree(m_popup_node);
}

Position Popup::preferred_position() const
{
    return m_preferred_position;
}

void Popup::set_preferred_position(Position preferred_position)
{
    m_preferred_position = preferred_position;
}

void Popup::root_item_about_to_update()
{
    // Before any tree change, attempt to close itself
    close();
}

void Popup::on_about_to_show() {}

void Popup::on_about_to_close() {}

void Popup::prepend(ObjectPtr child)
{
    Object::prepend(std::move(child));
}

void Popup::append(ObjectPtr child)
{
    Object::append(std::move(child));
}

void Popup::insert(ObjectPtr child, size_t index)
{
    Object::insert(std::move(child), index);
}

ObjectPtr Popup::remove(Object* child)
{
    return Object::remove(child);
}

bool Popup::allow_fallback_position() const
{
    return m_allow_fallback_position;
}

void Popup::set_allow_fallback_position(bool allow_fallback_position)
{
    m_allow_fallback_position = allow_fallback_position;
}

AlignU Popup::preferred_aligment() const
{
    return m_preferred_aligment;
}

void Popup::set_preferred_aligment(AlignU preferred_aligment)
{
    m_preferred_aligment = preferred_aligment;
}

Popup::Callbacks& Popup::callbacks()
{
    return m_callbacks;
}

void Popup::attach_to_item(
    Item* item,
    Position prefered_position,
    const Unit& offset,
    AlignU prefered_alignment
)
{
    ASSERT(item);
    m_attached_type      = AttachedType::Item;
    m_attached_to        = item;
    m_preferred_position = prefered_position;
    m_preferred_aligment = prefered_alignment;
    set_offset(offset);
    m_content_item->set_position_by_yoga(true);
}

void Popup::attach_to_center()
{
    m_attached_type = AttachedType::Center;
    m_attached_to   = nullptr;
    m_content_item->set_position_by_yoga(true);

    YGNodeStyleSetJustifyContent(m_popup_node, YGJustifyCenter);
    YGNodeStyleSetAlignItems(m_popup_node, YGAlignCenter);
}

bool Popup::opened() const
{
    return m_opened;
}

void Popup::open()
{
    ASSERT(m_content_item);

    ASSERT(root_item(), "Cannot open popup which is not inserted into complete tree");

    if (!m_opened) {
        on_about_to_show();

        RootItem* root = dynamic_cast<RootItem*>(root_item());
        root->open_popup(this);
        m_opened = true;
        YGNodeStyleSetDisplay(m_popup_node, YGDisplayFlex);
        m_content_item->set_visible(true);

        if (m_callbacks.opened) {
            m_callbacks.opened();
        }
    }
}

void Popup::close()
{
    ASSERT(m_content_item);
    if (!m_opened) {
        return;
    }

    RootItem* root = dynamic_cast<RootItem*>(root_item());
    ASSERT(root, "Unclosed popup is not inserted into complete tree");
    if (root) {
        on_about_to_close();

        root->close_popup(this);
        m_opened = false;
        YGNodeStyleSetDisplay(m_popup_node, YGDisplayNone);
        m_content_item->set_visible(false);

        if (m_callbacks.closed) {
            m_callbacks.closed();
        }
    }
}

Window* Popup::content_item() const
{
    return m_content_item;
}

void Popup::render(const Vec2f& pos, const Vec2f& size)
{
    if (!m_opened || !m_content_item) {
        return;
    }

    bool needs_resize = !Domain::fuzzy_compare(m_last_size.x(), size.x())
        || !Domain::fuzzy_compare(m_last_size.y(), size.y());

    if (!needs_resize && m_attached_type == AttachedType::Item) {
        const Vec2f current_pos = m_attached_to->get_global_pos();
        needs_resize            = !Domain::fuzzy_compare(current_pos.x(), m_last_attached_pos.x())
            || !Domain::fuzzy_compare(current_pos.y(), m_last_attached_pos.y());
    }

    if (needs_resize) {
        resize(m_last_size_info);
    }

    m_content_item->render(
        Vec2f(m_content_item->left(), m_content_item->top()),
        Vec2f(m_content_item->width(), m_content_item->height())
    );
}

void Popup::check_resized()
{
    if (m_opened) {
        m_content_item->check_resized();
    }
}

void Popup::resize(const SizeInfo& size_info)
{
    if (m_last_size_info != size_info) {
        m_last_size_info = size_info;
        m_last_size      = Vec2f{size_info.viewport_size_x, size_info.viewport_size_y};
        m_offset.evaluate(size_info);
    }

    if (!m_opened) {
        return;
    }

    YGNodeCalculateLayout(
        m_popup_node,
        size_info.viewport_size_x,
        size_info.viewport_size_y,
        YGDirectionLTR
    );

    // Free standing windows have all handling implemented in Window class
    if (m_attached_type == AttachedType::FreeStanding) {
        return;
    }

    ImRect popup_rect;
    if (m_attached_type == AttachedType::Item) {
        const Vec2f attachee_global_pos = m_attached_to->get_global_pos();
        m_last_attached_pos             = attachee_global_pos;
        const ImRect size_rect(0, 0, size_info.viewport_size_x, size_info.viewport_size_y);
        const ImVec2 target_pos{attachee_global_pos.x(), attachee_global_pos.y()};
        const ImRect target_rect{
            target_pos,
            target_pos + ImVec2{m_attached_to->width(), m_attached_to->height()}
        };
        const ImVec2 attachee_size(m_content_item->width(), m_content_item->height());

        std::list<Position>
            available_positions{Position::Left, Position::Right, Position::Top, Position::Bottom};
        std::erase(available_positions, m_preferred_position);

        std::pair<bool, ImRect> placed = try_to_place_popup(
            size_rect,
            target_rect,
            attachee_size,
            m_offset.result,
            m_preferred_position,
            m_preferred_aligment
        );
        ImRect preferred_rect = placed.second;
        if (!placed.first && m_allow_fallback_position) {
            // fallback to all other positions
            while (!placed.first && !available_positions.empty()) {
                placed = try_to_place_popup(
                    size_rect,
                    target_rect,
                    attachee_size,
                    m_offset.result,
                    available_positions.front(),
                    AlignU::Center
                );
                available_positions.pop_front();
            }
        }
        popup_rect = placed.first ? placed.second : preferred_rect;
    } else if (m_attached_type == AttachedType::Center) {
        popup_rect.Min.x = m_last_size.x() * 0.5f - m_content_item->width() * 0.5f;
        popup_rect.Min.y = m_last_size.y() * 0.5f - m_content_item->height() * 0.5f;
        popup_rect.Max = popup_rect.Min + ImVec2(m_content_item->width(), m_content_item->height());
    }

    Imgui::move_window_to_bounds({m_last_size.x(), m_last_size.y()}, popup_rect);

    if (m_attached_type != AttachedType::FreeStanding) {
        if (!Domain::fuzzy_compare(m_content_item->left(), popup_rect.Min.x)) {
            m_content_item->set_left(popup_rect.Min.x);
        }
        if (!Domain::fuzzy_compare(m_content_item->top(), popup_rect.Min.y)) {
            m_content_item->set_top(popup_rect.Min.y);
        }
    }

    YGNodeCalculateLayout(m_popup_node, m_last_size.x(), m_last_size.y(), YGDirectionLTR);

    // resolve size change
}

void Popup::set_content_item(WindowPtr content_item)
{
    ASSERT(content_item.get());
    if (m_content_item) {
        YGNodeRemoveChild(m_popup_node, m_content_item->node());
        remove(m_content_item);
    }
    m_content_item = content_item.get();
    append(std::move(content_item));
    YGNodeInsertChild(m_popup_node, m_content_item->node(), 0);
    m_content_item->set_position_type(YGPositionTypeAbsolute);
}

const Unit& Popup::offset() const
{
    return m_offset.source;
}

void Popup::set_offset(const Unit& offset)
{
    if (m_offset.source != offset) {
        m_offset.source = offset;
        m_offset.evaluate(m_last_size_info);
        set_style_dirty();
    }
}

void Popup::open_at(const Vec2f& pos)
{
    ASSERT(m_attached_type == AttachedType::FreeStanding, "open_at requires FreeStanding mode");
    m_content_item->set_left(pos.x());
    m_content_item->set_top(pos.y());
    open();
}

} // namespace Slic3r::App::Yoga
