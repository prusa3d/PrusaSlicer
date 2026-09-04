#include "Slic3r/App/Plater/MMPaintingColorSelector.hpp"

#include "Slic3r/App/Yoga/Circle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/TwoColorRing.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

ColorButton::ColorButton(
    const std::string& label,
    const Domain::ColorRGBA& color,
    ImColor mouse_left_color,
    ImColor mouse_right_color
) :
    m_inner_color(color.r_uchar(), color.g_uchar(), color.b_uchar(), color.a_uchar()),
    m_inner_color_hovered(Imgui::adjust_brightness(m_inner_color, 1.4f)),
    m_mouse_left_color{mouse_left_color},
    m_mouse_right_color{mouse_right_color}
{
    const Unit size{30_fpx};

    set_width(size);
    set_height(size);
    set_object_name("ColorButton");

    set_position_type(YGPositionType::YGPositionTypeRelative);
    set_align_items(YGAlignCenter);
    set_justify_content(YGJustifyCenter);

    m_highlight_circle = emplace_back<Yoga::TwoColorRing>();
    m_highlight_circle->set_flex_grow(1);
    m_highlight_circle->set_align_items(YGAlignCenter);
    m_highlight_circle->set_justify_content(YGJustifyCenter);
    m_highlight_circle->set_padding(7_fpx);

    m_color_circle = m_highlight_circle->emplace_back<Yoga::Circle>();
    m_color_circle->set_flex_grow(1);
    m_color_circle->set_align_items(YGAlignCenter);
    m_color_circle->set_justify_content(YGJustifyCenter);
    update_inner_circle();

    auto text{m_color_circle->emplace_back<Yoga::Text>(label)};
    text->set_font_type(Render::ImguiFontType::Bold);
    text->set_text_color(Imgui::contrast_color(m_inner_color));
    text->set_margin(Margins(0, -1, 0, 0));
}

void ColorButton::hovered_updated_internal()
{
    update_inner_circle();
}

void ColorButton::action_internal()
{
    on_color_selected(SelectedColor::Primary);
}

void ColorButton::secondary_action_internal()
{
    on_color_selected(SelectedColor::Secondary);
}

void ColorButton::update_inner_circle()
{
    m_color_circle->set_fill(hovered() ? m_inner_color_hovered : m_inner_color);
}

void ColorButton::select_color(SelectedColor color)
{
    if (color == SelectedColor::Primary) {
        if (m_selected_color == SelectedColor::Primary || m_selected_color == SelectedColor::Both) {
            return;
        }
        if (m_selected_color == SelectedColor::Secondary) {
            m_selected_color = SelectedColor::Both;
        }
        if (m_selected_color == SelectedColor::None) {
            m_selected_color = SelectedColor::Primary;
        }
    } else if (color == SelectedColor::Secondary) {
        if (m_selected_color == SelectedColor::Secondary || m_selected_color == SelectedColor::Both)
        {
            return;
        }
        if (m_selected_color == SelectedColor::Primary) {
            m_selected_color = SelectedColor::Both;
        }
        if (m_selected_color == SelectedColor::None) {
            m_selected_color = SelectedColor::Secondary;
        }
    }

    update_highlight_circle();
}

bool ColorButton::primary_selected() const
{
    return m_selected_color == SelectedColor::Primary || m_selected_color == SelectedColor::Both;
}

bool ColorButton::secondary_selected() const
{
    return m_selected_color == SelectedColor::Secondary || m_selected_color == SelectedColor::Both;
}

void ColorButton::clear_color(SelectedColor color)
{
    if (color == SelectedColor::Primary) {
        if (m_selected_color == SelectedColor::Primary) {
            m_selected_color = SelectedColor::None;
        }
        if (m_selected_color == SelectedColor::Both) {
            m_selected_color = SelectedColor::Secondary;
        }
    } else if (color == SelectedColor::Secondary) {
        if (m_selected_color == SelectedColor::Secondary) {
            m_selected_color = SelectedColor::None;
        }
        if (m_selected_color == SelectedColor::Both) {
            m_selected_color = SelectedColor::Primary;
        }
    } else {
        PANIC("Can clear only primary or secondary color");
    }
    update_highlight_circle();
}

void ColorButton::update_highlight_circle()
{
    if (primary_selected()) {
        m_highlight_circle->primary_color = m_mouse_left_color;
    } else {
        m_highlight_circle->primary_color = std::nullopt;
    }

    if (secondary_selected()) {
        m_highlight_circle->secondary_color = m_mouse_right_color;
    } else {
        m_highlight_circle->secondary_color = std::nullopt;
    }
}

ColorSelector::ColorSelector(ImColor mouse_left_color, ImColor mouse_right_color) :
    m_mouse_left_color{mouse_left_color},
    m_mouse_right_color{mouse_right_color}
{
    set_object_name("ColorSelector");
    set_flex_wrap(YGWrap::YGWrapWrap);
    set_gap(3);
}

void ColorSelector::set_colors(const PaintingPalette& palette)
{
    while (!m_children.empty()) {
        remove(m_children.back().get());
    }

    m_selectors.clear();

    for (std::size_t index{}; index < palette.size(); ++index) {
        const Domain::ColorRGBA& color{palette[index].color};
        m_selectors.push_back(
            emplace_back<ColorButton>(
                std::to_string(index + 1),
                color,
                m_mouse_left_color,
                m_mouse_right_color
            )
        );
    }

    for (std::size_t index{}; index < m_selectors.size(); ++index) {
        ColorButton* selector{m_selectors[index]};
        selector->on_color_selected = [this, index](SelectedColor selected_color)
        {
            select_color_index(selected_color, index);
            on_color_selected(selected_color, index);
        };
    }
}

void ColorSelector::select_color_index(SelectedColor color, std::size_t index)
{
    if (index >= m_selectors.size()) {
        return;
    }
    for (std::size_t i{}; i < m_selectors.size(); ++i) {
        ColorButton* selector{m_selectors[i]};
        if (i == index) {
            selector->select_color(color);
        } else {
            selector->clear_color(color);
        }
    }
}

std::size_t ColorSelector::colors_count() const
{
    return m_selectors.size();
}

} // namespace Slic3r::App::Plater
