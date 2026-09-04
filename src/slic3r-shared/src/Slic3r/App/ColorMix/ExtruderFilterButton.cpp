#include "Slic3r/App/ColorMix/ExtruderFilterButton.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

#include <cmath>
#include <limits>
#include <string>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::ColorMix {

ExtruderFilterButton::ExtruderFilterButton(unsigned int extruder_id_1based, const ImColor& color) :
    Yoga::AbstractButton({}, "ExtruderFilterButton"),
    m_extruder_id_1based(extruder_id_1based),
    m_color(color),
    m_swatch_inset{3_fpx},
    m_swatch_rounding{4_fpx},
    m_cross_inset{3_fpx},
    m_cross_thickness{2_fpx},
    m_border_thickness{1_fpx},
    m_dash_length{6_fpx},
    m_dash_gap{4_fpx},
    m_hover_ring_inset{1_fpx},
    m_hover_ring_thickness{1_fpx}
{
    this->set_width(34_fpx);
    this->set_height(34_fpx);
    this->set_flex_shrink(0);
    this->set_checkable(true);
    this->set_checked(true);
    this->set_cursor(ImGuiMouseCursor_Hand);
}

void ExtruderFilterButton::size_info_changed(const Yoga::SizeInfo& info_size)
{
    m_swatch_inset.evaluate(info_size);
    m_swatch_rounding.evaluate(info_size);
    m_cross_inset.evaluate(info_size);
    m_cross_thickness.evaluate(info_size);
    m_border_thickness.evaluate(info_size);
    m_dash_length.evaluate(info_size);
    m_dash_gap.evaluate(info_size);
    m_hover_ring_inset.evaluate(info_size);
    m_hover_ring_thickness.evaluate(info_size);
}

void ExtruderFilterButton::render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size)
{
    Yoga::AbstractButton::render(pos, size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    const ImVec2 rect_min{std::round(pos.x()), std::round(pos.y())};
    const ImVec2 rect_max{std::round(pos.x() + size.x()), std::round(pos.y() + size.y())};

    // The swatch is inset so that the accent ring of the hovered state has room around it.
    const float swatch_inset    = m_swatch_inset.result;
    const float swatch_rounding = m_swatch_rounding.result;
    const ImVec2 swatch_min{rect_min.x + swatch_inset, rect_min.y + swatch_inset};
    const ImVec2 swatch_max{rect_max.x - swatch_inset, rect_max.y - swatch_inset};

    // Unchecked buttons wash the extruder color out towards light grey instead of hiding it.
    const ImColor fill = this->checked() ?
        m_color :
        ImColor(
            (m_color.Value.x + 2.f * 0.75f) / 3.f,
            (m_color.Value.y + 2.f * 0.75f) / 3.f,
            (m_color.Value.z + 2.f * 0.75f) / 3.f
        );
    draw_list->AddRectFilled(swatch_min, swatch_max, fill, swatch_rounding);

    ImFont* bold_font       = m_imgui_render->font(Render::ImguiFontType::Bold);
    const float font_size   = ImGui::GetFontSize();
    const std::string label = std::to_string(m_extruder_id_1based);
    const float max_width   = std::numeric_limits<float>::max();
    const ImVec2 text_size  = bold_font->CalcTextSizeA(font_size, max_width, 0.f, label.c_str());
    draw_list->AddText(
        bold_font,
        font_size,
        ImVec2(
            swatch_min.x + (swatch_max.x - swatch_min.x - text_size.x) / 2.f,
            swatch_min.y + (swatch_max.y - swatch_min.y - text_size.y) / 2.f
        ),
        Imgui::contrast_color(fill),
        label.c_str()
    );

    const ImColor border_color = m_theme->color_imgui(Platform::Color::Text);
    if (this->checked()) {
        draw_list->AddRect(
            swatch_min,
            swatch_max,
            border_color,
            swatch_rounding,
            0,
            m_border_thickness.result
        );
    } else {
        Imgui::draw_dashed_rounded_rect(
            draw_list,
            swatch_min,
            swatch_max,
            border_color,
            m_border_thickness.result,
            swatch_rounding,
            m_dash_length.result,
            m_dash_gap.result
        );

        const float cross_inset = m_cross_inset.result;
        draw_list->AddLine(
            ImVec2(swatch_min.x + cross_inset, swatch_min.y + cross_inset),
            ImVec2(swatch_max.x - cross_inset, swatch_max.y - cross_inset),
            Imgui::contrast_color(fill),
            m_cross_thickness.result
        );
    }

    if (this->hovered() && this->enabled()) {
        const float hover_ring_inset = m_hover_ring_inset.result;
        draw_list->AddRect(
            ImVec2(rect_min.x + hover_ring_inset, rect_min.y + hover_ring_inset),
            ImVec2(rect_max.x - hover_ring_inset, rect_max.y - hover_ring_inset),
            m_theme->color_imgui(Platform::Color::AccentPrimary),
            swatch_rounding,
            0,
            m_hover_ring_thickness.result
        );
    }
}

} // namespace Slic3r::App::ColorMix
