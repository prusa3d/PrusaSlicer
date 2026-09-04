#include "Slic3r/App/Yoga/Rectangle.hpp"

#include <imgui_internal.h>

namespace Slic3r::App::Yoga {

Rectangle::Rectangle() : Item()
{
    set_object_name("Rectangle");
}

void Rectangle::render(const Vec2f& pos, const Vec2f& size)
{
    render_item_begin(pos, size);

    ImRect rect(to_im(pos), to_im(pos + size));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    ImColor fill_color =
        enabled() ? m_fill : (m_disabled_fill.has_value() ? m_disabled_fill.value() : m_fill);

    draw_list->AddRectFilled(rect.Min, rect.Max, fill_color, m_rounding, m_flags);
    if (m_border_width > 0) {
        draw_list->AddRect(rect.Min, rect.Max, m_border_color, m_rounding, m_flags, m_border_width);
    }

    render_item_end(pos, size);
}

const ImColor& Rectangle::fill() const
{
    return m_fill;
}

std::optional<ImColor> Rectangle::disabled_fill() const
{
    return m_disabled_fill;
}

const ImColor& Rectangle::border_color() const
{
    return m_border_color;
}

const ImColor& Rectangle::border_color_disabled() const
{
    return m_border_color_disabled;
}

float Rectangle::border_width() const
{
    return m_border_width;
}

float Rectangle::rounding() const
{
    return m_rounding;
}

ImDrawFlags Rectangle::flags() const
{
    return m_flags;
}

void Rectangle::set_fill(const ImColor& fill)
{
    m_fill = fill;
}

void Rectangle::set_disabled_fill(const ImColor& fill)
{
    m_disabled_fill = fill;
}

void Rectangle::set_border_color(const ImColor& border_color)
{
    m_border_color = border_color;
}

void Rectangle::set_border_color_disabled(const ImColor& border_color_disabled)
{
    m_border_color_disabled = border_color_disabled;
}

void Rectangle::set_border_width(float border_width)
{
    m_border_width = border_width;
}

void Rectangle::set_rounding(float rounding)
{
    m_rounding = rounding;
}

void Rectangle::set_flags(ImDrawFlags flags)
{
    m_flags = flags;
}

Vec2f Rectangle::get_item_size()
{
    return {0, 0};
}

} // namespace Slic3r::App::Yoga
