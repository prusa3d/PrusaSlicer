#include "Slic3r/App/ColorMix/BlendRecipeTile.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <cmath>
#include <limits>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::ColorMix {

BlendRecipeTile::
    BlendRecipeTile(const ImColor& blend_color, const std::string& label, bool already_used) :
    AbstractButton({}, "BlendRecipeTile"),
    m_blend_color(blend_color),
    m_label(label),
    m_already_used(already_used),
    m_tile_inset{3_fpx},
    m_tile_rounding{6_fpx},
    m_ring_rounding{4_fpx},
    m_ring_thickness{2_fpx},
    m_border_thickness{1_fpx},
    m_dash_length{6_fpx},
    m_dash_gap{4_fpx},
    m_text_pad{5_fpx}
{
    this->set_width(54_fpx);
    this->set_height(54_fpx);
    this->set_flex_shrink(0);
    this->set_cursor(ImGuiMouseCursor_Hand);
}

void BlendRecipeTile::size_info_changed(const Yoga::SizeInfo& info_size)
{
    m_tile_inset.evaluate(info_size);
    m_tile_rounding.evaluate(info_size);
    m_ring_rounding.evaluate(info_size);
    m_ring_thickness.evaluate(info_size);
    m_border_thickness.evaluate(info_size);
    m_dash_length.evaluate(info_size);
    m_dash_gap.evaluate(info_size);
    m_text_pad.evaluate(info_size);
}

bool BlendRecipeTile::already_used() const
{
    return m_already_used;
}

void BlendRecipeTile::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    Yoga::AbstractButton::render(pos, size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    const ImVec2 rect_min{std::round(pos.x()), std::round(pos.y())};
    const ImVec2 rect_max{std::round(pos.x() + size.x()), std::round(pos.y() + size.y())};

    // The tile is inset so that the accent ring of the used state has room around it.
    const float tile_inset = m_tile_inset.result;
    const ImVec2 tile_min{rect_min.x + tile_inset, rect_min.y + tile_inset};
    const ImVec2 tile_max{rect_max.x - tile_inset, rect_max.y - tile_inset};

    draw_list->AddRectFilled(tile_min, tile_max, m_blend_color, m_tile_rounding.result);

    ImFont* bold_font      = m_imgui_render->font(Render::ImguiFontType::Bold);
    const float font_size  = ImGui::GetFontSize() * 0.8f;
    const float text_pad   = m_text_pad.result;
    const float max_width  = std::numeric_limits<float>::max();
    const ImVec2 text_size = bold_font->CalcTextSizeA(font_size, max_width, 0.f, m_label.c_str());
    draw_list->AddText(
        bold_font,
        font_size,
        ImVec2(tile_max.x - text_size.x - text_pad, tile_max.y - text_size.y - text_pad),
        Imgui::contrast_color(m_blend_color),
        m_label.c_str()
    );

    const ImColor border_color =
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
    draw_list->AddRect(
        tile_min,
        tile_max,
        border_color,
        m_tile_rounding.result,
        0,
        m_border_thickness.result
    );

    const ImColor accent_color = m_theme->color_imgui(Platform::Color::AccentPrimary);
    if (m_already_used) {
        Imgui::draw_dashed_rounded_rect(
            draw_list,
            rect_min,
            rect_max,
            accent_color,
            m_ring_thickness.result,
            m_ring_rounding.result,
            m_dash_length.result,
            m_dash_gap.result
        );
    } else if (this->hovered() && this->enabled()) {
        draw_list->AddRect(
            rect_min,
            rect_max,
            accent_color,
            m_ring_rounding.result,
            0,
            m_ring_thickness.result
        );
    }
}

} // namespace Slic3r::App::ColorMix
