#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/Slider.hpp"
#include "Slic3r/App/Yoga/Circle.hpp"

#include "imgui/imgui_internal.h"
#include "Slic3r/Math.hpp"

namespace Slic3r::App::Yoga {

Slider::Callbacks& Slider::callbacks()
{
    return m_callbacks;
}

Slider::Slider(double begin, double end, double step) :
    Oval(),
    m_begin_value(begin),
    m_end_value(end),
    m_step(step),
    m_value(m_begin_value)
{
    set_object_name("Slider");
    set_fill(IM_COL32_BLACK_TRANS);
    set_border_color(m_theme->color_imgui(Platform::Color::Button));
    set_border_width(1.f);

    m_area = emplace_back<Oval>();
    m_area->set_object_name("SliderArea");
    m_area->set_fill(m_theme->color_imgui(Platform::Color::Button));
    m_area->set_disabled_fill(
        m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Disabled)
    );
    m_area->set_justify_content(YGJustifyFlexEnd);

    m_thumb = m_area->emplace_back<Circle>();
    m_thumb->set_object_name("SliderThumb");
    m_thumb->set_fill(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
    m_thumb->set_disabled_fill(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
    m_thumb->set_padding(3.f);
    m_thumb->set_min_width(14);
    m_thumb->set_min_height(14);

    m_knob = m_thumb->emplace_back<Circle>();
    m_knob->set_fill(m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active));
    m_knob->set_disabled_fill(
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled)
    );
}

Slider::Slider() :
    Slider(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(), 1.f)
{}

void Slider::set_hovered(bool hovered)
{
    if (m_hovered != hovered) {
        m_hovered = hovered;

        // updated fill on hovering change
        m_area->set_fill(m_theme->color_imgui(
            Platform::Color::Button,
            m_hovered ? Platform::ColorGroup::Hovered : Platform::ColorGroup::Default
        ));
        m_thumb->set_padding(m_hovered ? 0 : 3);
    }
}

double Slider::clamp(double value)
{
    if (m_begin_value < m_end_value)
        return std::clamp(value, m_begin_value, m_end_value);

    return std::clamp(value, m_end_value, m_begin_value);
}

double Slider::snap_to_nearest(double value)
{
    double step = m_step;
    if (m_begin_value > m_end_value)
        step *= -1;

    int pos = std::round((value - m_begin_value) / step);
    return m_begin_value + pos * step;
}

void Slider::update_area_width()
{
    if (!Domain::fuzzy_compare(m_end_value, m_begin_value)) {
        float ratio =
            static_cast<float>(fabs(m_value - m_begin_value) / fabs(m_end_value - m_begin_value));
        m_area->set_width(std::lerp(m_thumb->width(), width(), ratio));
    }
}

void Slider::render(const Vec2f& pos, const Vec2f& size)
{
    // Fix for thumb position after firts show
    if (!m_is_set_thumb_size && m_thumb->width() > 0) {
        m_is_set_thumb_size = true;
        update_area_width();
    }

    // Note: Temporary workaround to suppress slider editing when it's disabled.
    // Main fix will be done in SPE-3380.
    if (!enabled()) {
        Oval::render(pos, size);
        return;
    }

    ImRect ctrl_rc(to_im(pos), to_im(pos + size));
    set_hovered(ImGui::IsMouseHoveringRect(ctrl_rc.Min, ctrl_rc.Max, false));

    // Process interacting with the slider
    double step = m_step;
    if (m_begin_value > m_end_value)
        step *= -1.;

    if (m_hovered && ImGui::IsMouseClicked(0)) {
        m_dragging = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    // drag behavior
    if (m_dragging) {
        if (!io.MouseDown[0] || io.MouseReleased[0]) {
            m_dragging = false;
        } else {
            const double proc_pos = pos.x() + 0.5 * m_thumb->width();
            const double proc_width =
                static_cast<double>(std::max(0.f, width() - m_thumb->width()));

            double mouse_abs_pos   = io.MousePos[ImGuiAxis_X];
            double mouse_pos_ratio = Domain::fuzzy_compare(proc_width, 0.) ?
                0. :
                ImClamp((mouse_abs_pos - proc_pos) / proc_width, 0., 1.);

            double values_cnt = (m_end_value - m_begin_value) / step;
            double value      = m_begin_value + step * std::round(values_cnt * mouse_pos_ratio);
            set_value(value);
        }
    }
    // wheel behavior
    else if (m_hovered)
    {
        double mw    = sign(io.MouseWheel);
        double accer = io.KeyCtrl || io.KeyShift ? 5. : 1.;
        double value = clamp(m_value + mw * accer * step);

        if (!Domain::fuzzy_compare(m_value, value)) {
            set_style_dirty(); // to ask for redraw
        }
        set_value(value);
    }

    Oval::render(pos, size);
}

void Slider::set_value(double value)
{
    if (!Domain::fuzzy_compare(m_value, value)) {
        // update value
        m_value = snap_to_nearest(clamp(value));
        update_area_width();
        if (m_callbacks.value_changed)
            m_callbacks.value_changed(m_value);
    }
}

double Slider::value() const
{
    return m_value;
}

double Slider::begin() const
{
    return m_begin_value;
}

void Slider::set_begin_value(double begin)
{
    m_begin_value = begin;
}

double Slider::end() const
{
    return m_end_value;
}

void Slider::set_end_value(double end)
{
    m_end_value = end;
}

double Slider::step() const
{
    return m_step;
}

void Slider::set_step(double step)
{
    m_step = std::max(step, 0.);
}

void Slider::on_resized()
{
    update_area_width();
}
} // namespace Slic3r::App::Yoga
