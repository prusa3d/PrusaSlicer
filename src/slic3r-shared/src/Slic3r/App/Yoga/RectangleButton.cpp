#include "Slic3r/App/Yoga/RectangleButton.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"

namespace Slic3r::App::Yoga {

RectangleButton::RectangleButton(const std::string& tooltip) : AbstractButton(tooltip)
{
    set_object_name("RectangleButton");
    std::unique_ptr<Rectangle> rect = std::make_unique<Rectangle>();
    m_background                    = rect.get();
    AbstractButton::insert(std::move(rect), 0);
    m_background->set_padding(4_fpx);
    m_background->set_justify_content(YGJustifyCenter);
    m_background->set_gap(5_fpx);
    m_background->set_flex_grow(1);
    m_background->set_flex_shrink(0);

    set_background_color(Platform::Color::Button);
}

void RectangleButton::append(ObjectPtr child)
{
    m_background->append(std::move(child));
}

void RectangleButton::insert(ObjectPtr child, size_t index)
{
    m_background->insert(std::move(child), index);
}

ObjectPtr RectangleButton::remove(Object* child)
{
    return m_background->remove(child);
}

const ImColor& RectangleButton::background_color() const
{
    return m_background->fill();
}

void RectangleButton::set_background_color(Platform::Color color)
{
    m_background_color          = m_theme->color_imgui(color);
    m_background_color_hover    = m_theme->color_imgui(color, Platform::ColorGroup::Hovered);
    m_background_color_disabled = m_theme->color_imgui(color, Platform::ColorGroup::Disabled);
    set_background_color_checked(color);

    update_colors();
}

void RectangleButton::set_background_color(const ImColor& color)
{
    set_background_color(color, Imgui::adjust_brightness(color, 1.25f));
}

void RectangleButton::set_background_color(const ImColor& color, const ImColor& color_hover)
{
    m_background_color = color;

    m_background_color_hover    = color_hover;
    m_background_color_disabled = Imgui::adjust_brightness(m_background_color, 0.85f);

    update_colors();
}

const ImColor& RectangleButton::background_color_checked() const
{
    return m_background_color_checked;
}

void RectangleButton::set_background_color_checked(Platform::Color color)
{
    m_background_color_checked       = m_theme->color_imgui(color, Platform::ColorGroup::Active);
    m_background_color_checked_hover = Imgui::adjust_brightness(m_background_color_checked, 1.2f);
    m_background_color_checked_disabled =
        m_theme->color_imgui(color, Platform::ColorGroup::ActiveDisabled);

    update_colors();
}

void RectangleButton::set_background_color_checked(const ImColor& background_color_checked)
{
    set_background_color_checked(
        background_color_checked,
        Imgui::adjust_brightness(background_color_checked, 1.25f)
    );

    update_colors();
}

void RectangleButton::set_background_color_checked(
    const ImColor& background_color_checked,
    const ImColor& background_color_checked_hover
)
{
    m_background_color_checked       = background_color_checked;
    m_background_color_checked_hover = background_color_checked_hover;
    m_background_color_checked_disabled =
        Imgui::adjust_brightness(m_background_color_checked, 0.85f);
    update_colors();
}

const ImColor& RectangleButton::background_color_border() const
{
    return m_background_color_border;
}

void RectangleButton::set_background_color_border(const ImColor& background_color_border)
{
    set_background_color_border(
        background_color_border,
        Imgui::adjust_brightness(background_color_border, 1.25f)
    );
}

void RectangleButton::set_background_color_border(
    const ImColor& background_color_border,
    const ImColor& background_color_border_hover
)
{
    m_background_color_border          = background_color_border;
    m_background_color_border_hover    = background_color_border_hover;
    m_background_color_border_disabled = Imgui::adjust_brightness(m_background_color_border, 0.85f);
    update_colors();
}

const EvaluatedPaddings& RectangleButton::content_padding()
{
    return m_background->padding();
}

void RectangleButton::set_content_padding(const Paddings& padding)
{
    m_background->set_padding(padding);
}

const Unit& RectangleButton::content_gap()
{
    return m_background->gap();
}

void RectangleButton::set_content_gap(const Unit& gap)
{
    m_background->set_gap(gap);
}

Orientation RectangleButton::content_orientation() const
{
    return m_background->orientation();
}

void RectangleButton::set_content_orientation(Orientation orientation)
{
    m_background->set_orientation(orientation);
}

YGJustify RectangleButton::content_justify_content() const
{
    return m_background->justify_content();
}

YGAlign RectangleButton::content_align_item() const
{
    return m_background->align_items();
}

void RectangleButton::set_content_align_items(YGAlign align)
{
    m_background->set_align_items(align);
}

YGDirection RectangleButton::content_direction() const
{
    return direction();
}

void RectangleButton::set_content_direction(YGDirection direction)
{
    m_background->set_direction(direction);
}

float RectangleButton::background_border_width() const
{
    return m_background->border_width();
}

void RectangleButton::set_background_border_width(float width)
{
    m_background->set_border_width(width);
}

void RectangleButton::set_content_justify_content(YGJustify justify)
{
    m_background->set_justify_content(justify);
}

float RectangleButton::rounding() const
{
    return m_background->rounding();
}

void RectangleButton::set_rounding(float rounding)
{
    m_background->set_rounding(rounding);
}

ImDrawFlags RectangleButton::draw_flags() const
{
    return m_background->flags();
}

void RectangleButton::set_draw_flags(ImDrawFlags draw_flags)
{
    m_background->set_flags(draw_flags);
}

void RectangleButton::checked_updated_internal()
{
    update_colors();
}

void RectangleButton::hovered_updated_internal()
{
    update_colors();
}

void RectangleButton::update_colors()
{
    ImColor fill_color   = m_background_color;
    ImColor border_color = hovered() ? m_background_color_border_hover : m_background_color_border;
    if (checked()) {
        fill_color = hovered() ? m_background_color_checked_hover : m_background_color_checked;
    } else {
        fill_color = hovered() ? m_background_color_hover : m_background_color;
    }
    m_background->set_fill(fill_color);
    m_background->set_border_color(border_color);
    m_background->set_disabled_fill(
        checked() ? m_background_color_checked_disabled : m_background_color
    );
}

} // namespace Slic3r::App::Yoga
