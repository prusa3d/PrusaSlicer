#include "Slic3r/App/Yoga/Rectangle.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"

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

    if (m_apply_window_alpha) {
        fill_color.Value.w *= m_theme->transparent_window_alpha();
    }

    draw_list->AddRectFilled(rect.Min, rect.Max, fill_color, m_rounding, m_flags);
    if (m_border_width > 0) {
        if (m_border_dash_length > 0) {
            Imgui::draw_dashed_rounded_rect(
                draw_list,
                rect.Min,
                rect.Max,
                m_border_color,
                m_border_width,
                m_rounding,
                m_border_dash_length,
                m_border_gap_length
            );
        } else {
            draw_list
                ->AddRect(rect.Min, rect.Max, m_border_color, m_rounding, m_flags, m_border_width);
        }
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

float Rectangle::border_dash_length() const
{
    return m_border_dash_length;
}

float Rectangle::border_gap_length() const
{
    return m_border_gap_length;
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

void Rectangle::set_border_dash(float dash_length, float gap_length)
{
    m_border_dash_length = dash_length;
    m_border_gap_length  = gap_length;
}

void Rectangle::set_rounding(float rounding)
{
    m_rounding = rounding;
}

void Rectangle::set_flags(ImDrawFlags flags)
{
    m_flags = flags;
}

bool Rectangle::apply_window_alpha() const
{
    return m_apply_window_alpha;
}

void Rectangle::set_apply_window_alpha(bool apply_window_alpha)
{
    m_apply_window_alpha = apply_window_alpha;
}

Vec2f Rectangle::get_item_size()
{
    return {0, 0};
}

} // namespace Slic3r::App::Yoga
