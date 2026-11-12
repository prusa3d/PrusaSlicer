#include "Slic3r/App/ColorMix/BarycentricRatioPicker.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Render/ImguiRender.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

using Slic3r::Domain::Vec2d;

using Slic3r::Biz::Algorithms::VirtualExtruder::balanced_ratios_percent;

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::ColorMix {

namespace {

double signed_area2(const Vec2d& v0, const Vec2d& v1, const Vec2d& v2)
{
    return (v1.x() - v0.x()) * (v2.y() - v0.y()) - (v2.x() - v0.x()) * (v1.y() - v0.y());
}

bool contains(const Vec2d& p, const Vec2d& v0, const Vec2d& v1, const Vec2d& v2)
{
    const double a     = signed_area2(p, v0, v1);
    const double b     = signed_area2(p, v1, v2);
    const double c     = signed_area2(p, v2, v0);
    const bool has_neg = (a < 0) || (b < 0) || (c < 0);
    const bool has_pos = (a > 0) || (b > 0) || (c > 0);
    return !(has_neg && has_pos);
}

void barycentric(
    const Vec2d& p,
    const Vec2d& v0,
    const Vec2d& v1,
    const Vec2d& v2,
    double& weight_0,
    double& weight_1,
    double& weight_2
)
{
    const double total = signed_area2(v0, v1, v2);
    if (std::abs(total) < 1e-9) {
        weight_0 = weight_1 = weight_2 = 1.0 / 3.0;
        return;
    }

    weight_0 = signed_area2(p, v1, v2) / total;
    weight_1 = signed_area2(v0, p, v2) / total;
    weight_2 = 1.0 - weight_0 - weight_1;
}

void snap_weights_to_five_percent(double& weight_0, double& weight_1, double& weight_2)
{
    constexpr double third       = 1.0 / 3.0;
    constexpr double center_band = 0.05;
    if (std::abs(weight_0 - third) < center_band
        && std::abs(weight_1 - third) < center_band
        && std::abs(weight_2 - third) < center_band)
    {
        const std::vector<int> balanced = balanced_ratios_percent(3);
        weight_0 = balanced[0] / 100.0;
        weight_1 = balanced[1] / 100.0;
        weight_2 = balanced[2] / 100.0;
        return;
    }

    // Floor every weight, then give the remainder to the largest fractional parts so that the sum stays exact.
    constexpr int total_steps = 20;
    const double t0           = weight_0 * total_steps;
    const double t1           = weight_1 * total_steps;
    const double t2           = weight_2 * total_steps;
    int p0                    = static_cast<int>(std::floor(t0));
    int p1                    = static_cast<int>(std::floor(t1));
    int p2                    = static_cast<int>(std::floor(t2));
    p0                        = std::clamp(p0, 0, total_steps);
    p1                        = std::clamp(p1, 0, total_steps);
    p2                        = std::clamp(p2, 0, total_steps);

    int leftover = total_steps - (p0 + p1 + p2);
    std::array<std::pair<double, int>, 3> remainders{{
        {t0 - std::floor(t0), 0},
        {t1 - std::floor(t1), 1},
        {t2 - std::floor(t2), 2},
    }};
    std::ranges::sort(
        remainders,
        [](const std::pair<double, int>& a, const std::pair<double, int>& b)
        { return a.first > b.first; }
    );
    for (int i = 0; i < 3 && leftover > 0; ++i, --leftover) {
        if (remainders[i].second == 0) {
            ++p0;
        } else if (remainders[i].second == 1) {
            ++p1;
        } else {
            ++p2;
        }
    }

    std::ranges::sort(
        remainders,
        [](const std::pair<double, int>& a, const std::pair<double, int>& b)
        { return a.first < b.first; }
    );
    for (int i = 0; i < 3 && leftover < 0; ++i, ++leftover) {
        if (remainders[i].second == 0 && p0 > 0) {
            --p0;
        } else if (remainders[i].second == 1 && p1 > 0) {
            --p1;
        } else if (remainders[i].second == 2 && p2 > 0) {
            --p2;
        } else {
            ++leftover;
        }
    }

    weight_0 = static_cast<double>(p0) / static_cast<double>(total_steps);
    weight_1 = static_cast<double>(p1) / static_cast<double>(total_steps);
    weight_2 = static_cast<double>(p2) / static_cast<double>(total_steps);
}

} // namespace

BarycentricRatioPicker::BarycentricRatioPicker() :
    AbstractButton({}, "BarycentricRatioPicker"),
    m_triangle_margin{22_fpx},
    m_badge_radius{12_fpx},
    m_badge_border_thickness{2_fpx},
    m_handle_radius{9_fpx},
    m_outline_thickness{1_fpx}
{
    this->set_min_width(180_fpx);
    this->set_min_height(180_fpx);
}

void BarycentricRatioPicker::size_info_changed(const Yoga::SizeInfo& info_size)
{
    m_triangle_margin.evaluate(info_size);
    m_badge_radius.evaluate(info_size);
    m_badge_border_thickness.evaluate(info_size);
    m_handle_radius.evaluate(info_size);
    m_outline_thickness.evaluate(info_size);
}

BarycentricRatioPicker::Callbacks& BarycentricRatioPicker::callbacks()
{
    return m_callbacks;
}

void BarycentricRatioPicker::
    set_colors(const ImColor& color_0, const ImColor& color_1, const ImColor& color_2)
{
    m_color_0 = color_0;
    m_color_1 = color_1;
    m_color_2 = color_2;
    this->set_style_dirty();
}

void BarycentricRatioPicker::
    set_weights(const double weight_0, const double weight_1, const double weight_2)
{
    this->normalize_and_assign(weight_0, weight_1, weight_2);
    this->set_style_dirty();
}

void BarycentricRatioPicker::set_vertex_labels(
    const std::string& label_0,
    const std::string& label_1,
    const std::string& label_2
)
{
    m_label_0 = label_0;
    m_label_1 = label_1;
    m_label_2 = label_2;
    this->set_style_dirty();
}

void BarycentricRatioPicker::vertices(const Vec2f& size, Vec2d& v0, Vec2d& v1, Vec2d& v2) const
{
    const double panel_width  = std::max(1.f, size.x());
    const double panel_height = std::max(1.f, size.y());
    const double margin       = static_cast<double>(m_triangle_margin.result);
    v0                        = Vec2d{panel_width / 2.0, margin};
    v1                        = Vec2d{margin, panel_height - margin};
    v2                        = Vec2d{panel_width - margin, panel_height - margin};
}

void BarycentricRatioPicker::normalize_and_assign(double weight_0, double weight_1, double weight_2)
{
    weight_0         = std::clamp(weight_0, 0.0, 1.0);
    weight_1         = std::clamp(weight_1, 0.0, 1.0);
    weight_2         = std::clamp(weight_2, 0.0, 1.0);
    const double sum = weight_0 + weight_1 + weight_2;
    if (sum > 0) {
        m_weight_0 = weight_0 / sum;
        m_weight_1 = weight_1 / sum;
        m_weight_2 = weight_2 / sum;
    } else {
        m_weight_0 = m_weight_1 = m_weight_2 = 1.0 / 3.0;
    }
}

bool BarycentricRatioPicker::update_from_point(const Vec2f& size, const Vec2d& p)
{
    Vec2d v0, v1, v2;
    this->vertices(size, v0, v1, v2);

    double weight_0, weight_1, weight_2;
    barycentric(p, v0, v1, v2, weight_0, weight_1, weight_2);
    weight_0 = std::clamp(weight_0, 0.0, 1.0);
    weight_1 = std::clamp(weight_1, 0.0, 1.0);
    weight_2 = std::clamp(weight_2, 0.0, 1.0);

    const double sum = weight_0 + weight_1 + weight_2;
    if (sum > 0) {
        weight_0 /= sum;
        weight_1 /= sum;
        weight_2 /= sum;
    } else {
        weight_0 = weight_1 = weight_2 = 1.0 / 3.0;
    }

    snap_weights_to_five_percent(weight_0, weight_1, weight_2);
    if (std::abs(weight_0 - m_weight_0) < 1e-6
        && std::abs(weight_1 - m_weight_1) < 1e-6
        && std::abs(weight_2 - m_weight_2) < 1e-6)
    {
        return false;
    }

    m_weight_0 = weight_0;
    m_weight_1 = weight_1;
    m_weight_2 = weight_2;

    this->set_style_dirty();
    return true;
}

void BarycentricRatioPicker::render(const Vec2f& pos, const Vec2f& size)
{
    AbstractButton::render(pos, size);

    if (this->enabled()) {
        if (this->pressed()) {
            const ImGuiIO& io = ImGui::GetIO();
            const Vec2d local_point{io.MousePos.x - pos.x(), io.MousePos.y - pos.y()};
            if (!m_dragging) {
                Vec2d v0, v1, v2;
                this->vertices(size, v0, v1, v2);
                if (contains(local_point, v0, v1, v2)) {
                    m_dragging = true;
                }
            }

            if (m_dragging && this->update_from_point(size, local_point)) {
                if (m_callbacks.weights_changed) {
                    m_callbacks.weights_changed();
                }
            }
        } else {
            m_dragging = false;
        }
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ASSERT(draw_list);

    Vec2d v0, v1, v2;
    this->vertices(size, v0, v1, v2);
    const ImVec2 p0{pos.x() + static_cast<float>(v0.x()), pos.y() + static_cast<float>(v0.y())};
    const ImVec2 p1{pos.x() + static_cast<float>(v1.x()), pos.y() + static_cast<float>(v1.y())};
    const ImVec2 p2{pos.x() + static_cast<float>(v2.x()), pos.y() + static_cast<float>(v2.y())};

    // Gouraud-filled triangle: one vertex per component color, interpolated in between.
    draw_list->PrimReserve(3, 3);
    const ImDrawIdx base_index = static_cast<ImDrawIdx>(draw_list->_VtxCurrentIdx);
    draw_list->PrimWriteIdx(base_index);
    draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 1));
    draw_list->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 2));
    const ImVec2 white_uv = draw_list->_Data->TexUvWhitePixel;
    draw_list->PrimWriteVtx(p0, white_uv, m_color_0);
    draw_list->PrimWriteVtx(p1, white_uv, m_color_1);
    draw_list->PrimWriteVtx(p2, white_uv, m_color_2);

    const ImColor outline_color =
        m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
    draw_list->AddTriangle(p0, p1, p2, outline_color, m_outline_thickness.result);

    const ImColor badge_border_color   = m_theme->color_imgui(Platform::Color::WindowBg);
    ImFont* bold_font                  = m_imgui_render->font(Render::ImguiFontType::Bold);
    const float badge_font_size        = ImGui::GetFontSize() * 0.8f;
    const float badge_radius           = m_badge_radius.result;
    const float badge_border_thickness = m_badge_border_thickness.result;

    const auto draw_vertex_badge =
        [&](const ImVec2& center, const ImColor& fill, const std::string& label)
    {
        draw_list->AddCircleFilled(center, badge_radius, fill);
        draw_list->AddCircle(center, badge_radius, badge_border_color, 0, badge_border_thickness);

        const ImVec2 text_size = bold_font->CalcTextSizeA(
            badge_font_size,
            std::numeric_limits<float>::max(),
            0.f,
            label.c_str()
        );

        draw_list->AddText(
            bold_font,
            badge_font_size,
            ImVec2(center.x - text_size.x / 2.f, center.y - text_size.y / 2.f),
            Imgui::contrast_color(fill),
            label.c_str()
        );
    };

    draw_vertex_badge(p0, m_color_0, m_label_0);
    draw_vertex_badge(p1, m_color_1, m_label_1);
    draw_vertex_badge(p2, m_color_2, m_label_2);

    const float handle_x =
        static_cast<float>(m_weight_0 * v0.x() + m_weight_1 * v1.x() + m_weight_2 * v2.x());
    const float handle_y =
        static_cast<float>(m_weight_0 * v0.y() + m_weight_1 * v1.y() + m_weight_2 * v2.y());
    const ImVec2 handle_center{pos.x() + handle_x, pos.y() + handle_y};
    const float handle_radius = m_handle_radius.result;

    draw_list->AddCircleFilled(handle_center, handle_radius, ImColor(255, 255, 255));
    draw_list->AddCircle(handle_center, handle_radius, ImColor(0, 0, 0, 128), 0, 1.5f);
}

} // namespace Slic3r::App::ColorMix
