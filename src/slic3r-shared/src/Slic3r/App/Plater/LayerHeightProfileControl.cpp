#include "Slic3r/App/Plater/LayerHeightProfileControl.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include <cmath>
#include <imgui/imgui.h>
#include <ranges>

using Slic3r::Domain::Vec2f;
using Slic3r::Domain::ZHeightPair;
using Slic3r::Domain::ZHeightPairs;
using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

const constexpr float LAYER_HEIGHT_BASELINE_THICKNESS = 2.f;
const constexpr float LAYER_HEIGHT_PROFILE_THICKNESS  = 2.f;

LayerHeightProfileControl::LayerHeightProfileControl() : Item()
{
    set_object_name("LayerHeightProfileControl");
    set_flex_grow(1.f);

    const ImColor range_color = m_theme->color_imgui(Platform::Color::AccentSecondary);

    m_layer_height_profile_color              = ImColor(175, 119, 255, 255);
    m_layer_height_baseline_color             = m_theme->color_imgui(Platform::Color::Transparent);
    m_height_range_color_even                 = Imgui::adjust_brightness(range_color, 0.65f);
    m_height_range_color_odd                  = Imgui::adjust_brightness(range_color, 0.5f);
    m_height_range_color_selected             = range_color;
    m_height_range_color_hovered              = Imgui::adjust_brightness(range_color, 1.2f);
    m_height_range_color_overlap_fill         = m_theme->color_imgui(Platform::Color::Warning);
    m_height_range_color_overlap_fill.Value.w = 0.5f; // 50% transparent
    m_height_range_color_overlap_border       = m_theme->color_imgui(Platform::Color::Warning);
}

void LayerHeightProfileControl::set_object_max_z(const float object_max_z)
{
    m_object_max_z = object_max_z;
}

void LayerHeightProfileControl::set_min_layer_height(const float min_layer_height)
{
    m_min_layer_height = min_layer_height;
}

void LayerHeightProfileControl::set_max_layer_height(const float max_layer_height)
{
    m_max_layer_height = max_layer_height;
}

void LayerHeightProfileControl::set_default_layer_height(const float default_layer_height)
{
    m_default_layer_height = default_layer_height;
}

void LayerHeightProfileControl::set_layer_height_profile(const ZHeightPairs& layer_height_profile)
{
    m_layer_height_profile = layer_height_profile;
}

void LayerHeightProfileControl::set_height_ranges(const HeightRangeEntries& height_ranges)
{
    m_height_ranges = height_ranges;
    m_selected_range_index.reset();
    m_hovered_range_index.reset();
}

void LayerHeightProfileControl::set_selected_height_range(const size_t range_index)
{
    m_selected_range_index = range_index;
}

void LayerHeightProfileControl::set_hovered_height_range(const size_t range_index)
{
    m_hovered_range_index = range_index;
}

void LayerHeightProfileControl::set_external_hovered_height_range(const size_t range_index)
{
    m_hovered_range_index = range_index;
    m_external_hover      = true;
}

void LayerHeightProfileControl::reset_selected_height_range()
{
    m_selected_range_index.reset();
}

void LayerHeightProfileControl::reset_hovered_height_range()
{
    m_hovered_range_index.reset();
}

void LayerHeightProfileControl::reset_external_hovered_height_range()
{
    m_external_hover = false;
    m_hovered_range_index.reset();
}

void LayerHeightProfileControl::update_height_range(
    const size_t range_index,
    const double min_z,
    const double max_z,
    const std::optional<double> layer_height
)
{
    ASSERT(range_index < m_height_ranges.size());
    m_height_ranges[range_index].min_z = min_z;
    m_height_ranges[range_index].max_z = max_z;

    if (layer_height.has_value()) {
        m_height_ranges[range_index].layer_height = layer_height.value();
    }

    update_overlaps();
}

float LayerHeightProfileControl::object_max_z() const
{
    return m_object_max_z;
}

float LayerHeightProfileControl::min_layer_height_value() const
{
    return m_min_layer_height;
}

float LayerHeightProfileControl::max_layer_height_value() const
{
    return m_max_layer_height;
}

const HeightRangeEntries& LayerHeightProfileControl::height_ranges() const
{
    return m_height_ranges;
}

std::optional<size_t> LayerHeightProfileControl::selected_range_index() const
{
    return m_selected_range_index;
}

std::optional<size_t> LayerHeightProfileControl::hovered_range_index() const
{
    return m_hovered_range_index;
}

bool LayerHeightProfileControl::is_external_hover() const
{
    return m_external_hover;
}

void LayerHeightProfileControl::update_overlaps()
{
    m_overlaps.clear();
    for (size_t i = 0; i + 1 < m_height_ranges.size(); ++i) {
        for (size_t j = i + 1; j < m_height_ranges.size(); ++j) {
            const HeightRangeEntry& a = m_height_ranges[i];
            const HeightRangeEntry& b = m_height_ranges[j];

            const float min_z = std::max<float>(a.min_z, b.min_z);
            const float max_z = std::min<float>(a.max_z, b.max_z);

            if (min_z < max_z) {
                m_overlaps.emplace_back(min_z, max_z);
            }
        }
    }

    std::sort(m_overlaps.begin(), m_overlaps.end());
}

float LayerHeightProfileControl::project_layer_height(
    float layer_height,
    float out_range_min,
    float out_range_max
) const
{
    ASSERT(m_min_layer_height < m_max_layer_height);
    const float t = std::clamp(
        (layer_height - m_min_layer_height) / (m_max_layer_height - m_min_layer_height),
        0.f,
        1.f
    );
    return std::lerp(out_range_min, out_range_max, t);
}

float LayerHeightProfileControl::project_layer_z(
    float layer_z,
    float out_range_min,
    float out_range_max
) const
{
    ASSERT(m_min_layer_height < m_max_layer_height);
    const float t = std::clamp(1.f - (layer_z / m_object_max_z), 0.f, 1.f);
    return std::lerp(out_range_min, out_range_max, t);
}

float LayerHeightProfileControl::project_mouse_y_to_z(
    const float mouse_y,
    const Vec2f& pos,
    const Vec2f& size,
    const float object_max_z
) const
{
    const float t = std::clamp((mouse_y - pos.y()) / size.y(), 0.f, 1.f);
    return std::lerp(object_max_z, 0.f, t);
}

void LayerHeightProfileControl::render_baseline(const Vec2f& pos, const Vec2f& size) const
{
    if (m_min_layer_height == m_max_layer_height) {
        return;
    }

    const float baseline_pos_x =
        this->project_layer_height(m_default_layer_height, pos.x(), pos.x() + size.x());

    ImDrawList& draw_list = *ImGui::GetWindowDrawList();
    draw_list.AddLine(
        ImVec2(baseline_pos_x, pos.y()),
        ImVec2(baseline_pos_x, pos.y() + size.y()),
        m_layer_height_baseline_color,
        LAYER_HEIGHT_BASELINE_THICKNESS
    );
}

void
LayerHeightProfileControl::render_layer_height_profile(const Vec2f& pos, const Vec2f& size) const
{
    if (m_min_layer_height == m_max_layer_height) {
        return;
    }

    std::vector<ImVec2> profile_points;
    profile_points.reserve(m_layer_height_profile.size());

    for (const ZHeightPair& layer : m_layer_height_profile) {
        const float layer_z      = static_cast<float>(layer.z);
        const float layer_height = static_cast<float>(layer.layer_height);

        const float layer_z_pos_y = this->project_layer_z(layer_z, pos.y(), pos.y() + size.y());
        const float layer_height_pos_x =
            this->project_layer_height(layer_height, pos.x(), pos.x() + size.x());

        profile_points.emplace_back(layer_height_pos_x, layer_z_pos_y);
    }

    if (profile_points.empty()) {
        const float profile_pos_x =
            this->project_layer_height(m_default_layer_height, pos.x(), pos.x() + size.x());
        profile_points.emplace_back(profile_pos_x, pos.y());
        profile_points.emplace_back(profile_pos_x, pos.y() + size.y());
    }

    ImDrawList& draw_list = *ImGui::GetWindowDrawList();
    draw_list.AddPolyline(
        profile_points.data(),
        static_cast<int>(profile_points.size()),
        m_layer_height_profile_color,
        ImDrawFlags_None,
        LAYER_HEIGHT_PROFILE_THICKNESS
    );
}

void LayerHeightProfileControl::render_height_ranges(const Vec2f& pos, const Vec2f& size) const
{
    if (m_height_ranges.empty()
        || m_min_layer_height == m_max_layer_height
        || m_object_max_z <= 0.f)
    {
        return;
    }

    const bool has_hovered  = m_hovered_range_index.has_value();
    const bool has_selected = m_selected_range_index.has_value();

    ASSERT(!has_hovered || m_hovered_range_index.value() < m_height_ranges.size());
    ASSERT(!has_selected || m_selected_range_index.value() < m_height_ranges.size());

    ImDrawList& draw_list = *ImGui::GetWindowDrawList();

    const auto draw_range = [&](const size_t range_idx, const ImColor range_color)
    {
        const HeightRangeEntry& height_range = m_height_ranges[range_idx];

        const float top_y = this->project_layer_z(
            static_cast<float>(height_range.max_z),
            pos.y(),
            pos.y() + size.y()
        );

        const float bottom_y = this->project_layer_z(
            static_cast<float>(height_range.min_z),
            pos.y(),
            pos.y() + size.y()
        );

        draw_list.AddRectFilled(
            ImVec2(pos.x(), top_y),
            ImVec2(pos.x() + size.x(), bottom_y),
            range_color
        );
    };

    for (size_t i = 0; i < m_height_ranges.size(); ++i) {
        if (has_selected && i == m_selected_range_index.value()) {
            continue;
        } else if (has_hovered && i == m_hovered_range_index.value()) {
            continue;
        }

        draw_range(i, (i % 2 == 0) ? m_height_range_color_even : m_height_range_color_odd);
    }

    if (has_hovered
        && (!has_selected || m_hovered_range_index.value() != m_selected_range_index.value()))
    {
        draw_range(m_hovered_range_index.value(), m_height_range_color_hovered);
    }

    if (has_selected) {
        draw_range(m_selected_range_index.value(), m_height_range_color_selected);
    }

    // Draw overlaps
    for (size_t i = 0; i < m_overlaps.size();) {
        auto [min_z, max_z] = m_overlaps[i++];

        while (i < m_overlaps.size() && m_overlaps[i].first <= max_z) {
            max_z = std::max(max_z, m_overlaps[i++].second);
        }

        const float top_y    = this->project_layer_z(max_z, pos.y(), pos.y() + size.y());
        const float bottom_y = this->project_layer_z(min_z, pos.y(), pos.y() + size.y());

        const ImVec2 rect_min(pos.x(), top_y);
        const ImVec2 rect_max(pos.x() + size.x(), bottom_y);

        draw_list.AddRectFilled(rect_min, rect_max, m_height_range_color_overlap_fill);
        draw_list.AddRect(rect_min, rect_max, m_height_range_color_overlap_border, 0.0f, 0, 2.0f);
    }
}

std::optional<size_t> LayerHeightProfileControl::pick_height_range(
    const float mouse_y,
    const Vec2f& pos,
    const Vec2f& size
) const
{
    if (m_height_ranges.empty() || m_object_max_z <= 0.f) {
        return std::nullopt;
    }

    const float z = this->project_mouse_y_to_z(mouse_y, pos, size, m_object_max_z);
    if (m_selected_range_index.has_value()) {
        ASSERT(m_selected_range_index.value() < m_height_ranges.size());

        const HeightRangeEntry& height_range = m_height_ranges[m_selected_range_index.value()];
        if (height_range.min_z <= z && z <= height_range.max_z) {
            return m_selected_range_index.value();
        }
    }

    for (const HeightRangeEntry& height_range : m_height_ranges | std::views::reverse) {
        const size_t range_idx = &height_range - m_height_ranges.data();
        if (m_selected_range_index.has_value() && range_idx == m_selected_range_index.value()) {
            continue;
        }

        const HeightRangeEntry& range = m_height_ranges[range_idx];
        if (z >= range.min_z && z <= range.max_z) {
            return range_idx;
        }
    }

    return std::nullopt;
}

} // namespace Slic3r::App::Yoga
