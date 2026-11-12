#include "Slic3r/App/Plater/GizmoWindowWithLeftSidePanel.hpp"

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

GizmoWindowWithLeftSidePanel::GizmoWindowWithLeftSidePanel() : GizmoWindow()
{
    // Background rectangle for the side panel (with rounded corners on the left).
    m_side_panel = emplace<Rectangle>(0);
    m_side_panel->set_flex_shrink(0);
    m_side_panel->set_min_width(40);
    m_side_panel->set_max_width(80);
    m_side_panel->set_flex_grow(1);
    m_side_panel->set_orientation(Orientation::Vertical);
    m_side_panel->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    m_side_panel->set_flags(ImDrawFlags_RoundCornersLeft);
    m_side_panel->set_rounding(5.0f);
    m_side_panel->set_gap(5);
    m_side_panel->set_padding(Paddings{0, 5, 0, 0});

    m_side_panel_header_title = m_side_panel->emplace_back<Text>(std::string{});
    m_side_panel_header_title->set_align({AlignH::Center, AlignV::Center});
}

Text* GizmoWindowWithLeftSidePanel::side_panel_header_title() const
{
    return m_side_panel_header_title;
}

Rectangle* GizmoWindowWithLeftSidePanel::side_panel() const
{
    return m_side_panel;
}

} // namespace Slic3r::App::Plater
