#include "Slic3r/App/Plater/HeightRangeControl.hpp"

#include <imgui/imgui.h>
#include <ranges>

using Slic3r::Domain::Vec2f;

namespace Slic3r::App::Plater {

const constexpr float LAYER_HEIGHT_PROFILE_PADDING            = 5.f;
const constexpr float HEIGHT_RANGE_BORDER_DETECTION_THRESHOLD = 5.f;

HeightRangeControl::HeightRangeControl() : LayerHeightProfileControl()
{
    this->set_object_name("HeightRangeControl");
}

HeightRangeControl::Callbacks& HeightRangeControl::callbacks()
{
    return m_callbacks;
}

void HeightRangeControl::render(const Vec2f& pos, const Vec2f& size)
{
    if (size.x() <= 0.f || size.y() <= 0.f) {
        return;
    }

    this->render_item_begin(pos, size);

    const Vec2f profile_area_position{pos.x() + LAYER_HEIGHT_PROFILE_PADDING, pos.y()};
    const Vec2f profile_area_size{size.x() - 2.f * LAYER_HEIGHT_PROFILE_PADDING, size.y()};

    this->process_input(pos, size, profile_area_position, profile_area_size);

    this->render_baseline(profile_area_position, profile_area_size);
    this->render_height_ranges(profile_area_position, profile_area_size);
    this->render_layer_height_profile(profile_area_position, profile_area_size);

    this->render_item_end(pos, size);
}

HeightRangeControl::ZRange HeightRangeControl::compute_dragged_height_range(
    const float mouse_y,
    const Vec2f& pos,
    const Vec2f& size
) const
{
    float new_min_z = m_drag_initial_range_min_z;
    float new_max_z = m_drag_initial_range_max_z;

    if (m_drag_mode == DragMode::MinBorder) {
        new_min_z = this->project_mouse_y_to_z(mouse_y, pos, size, this->object_max_z());
        new_min_z = std::clamp(new_min_z, 0.f, new_max_z);
    } else if (m_drag_mode == DragMode::MaxBorder) {
        new_max_z = this->project_mouse_y_to_z(mouse_y, pos, size, this->object_max_z());
        new_max_z = std::clamp(new_max_z, new_min_z, this->object_max_z());
    } else if (m_drag_mode == DragMode::Body) {
        const float delta_z = this->project_mouse_y_to_z(mouse_y, pos, size, this->object_max_z())
            - this->project_mouse_y_to_z(m_drag_initial_mouse_y, pos, size, this->object_max_z());

        new_min_z += delta_z;
        new_max_z += delta_z;

        if (new_min_z < 0.f) {
            new_max_z -= new_min_z;
            new_min_z = 0.f;
        }

        if (new_max_z > this->object_max_z()) {
            new_min_z -= new_max_z - this->object_max_z();
            new_max_z = this->object_max_z();
        }

        new_min_z = std::max(0.f, new_min_z);
    }

    return {new_min_z, new_max_z};
}

void HeightRangeControl::process_input(
    const Vec2f& pos,
    const Vec2f& size,
    const Vec2f& profile_area_position,
    const Vec2f& profile_area_size
)
{
    ImGui::SetCursorScreenPos(ImVec2(pos.x(), pos.y()));
    ImGui::PushID(object_name().c_str());

    ImGui::InvisibleButton(
        "##height_range_control",
        ImVec2(size.x(), size.y()),
        ImGuiButtonFlags_MouseButtonLeft
    );

    const ImGuiIO& io  = ImGui::GetIO();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    if (hovered && m_drag_mode == DragMode::None) {
        this->reset_external_hovered_height_range();

        const std::optional<BorderHitResult> border_hit =
            this->pick_height_range_border(io.MousePos.y, profile_area_position, profile_area_size);
        if (border_hit.has_value()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        const std::optional<size_t> range_hit =
            this->pick_height_range(io.MousePos.y, profile_area_position, profile_area_size);
        if (range_hit.has_value()) {
            this->set_hovered_height_range(range_hit.value());
            m_callbacks.height_range_hovered(range_hit);
        } else {
            this->reset_hovered_height_range();
            m_callbacks.height_range_hovered(std::nullopt);
        }
    } else if (!hovered && m_drag_mode == DragMode::None) {
        if (m_was_hovered) {
            this->reset_external_hovered_height_range();
            this->reset_hovered_height_range();
            m_callbacks.height_range_hovered(std::nullopt);
        } else if (!this->is_external_hover()) {
            this->reset_hovered_height_range();
            m_callbacks.height_range_hovered(std::nullopt);
        }
    }

    if (clicked) {
        const std::optional<BorderHitResult> border_hit =
            this->pick_height_range_border(io.MousePos.y, profile_area_position, profile_area_size);
        if (border_hit.has_value()) {
            const HeightRangeEntry& range = this->height_ranges()[border_hit->range_index];
            m_drag_mode =
                (border_hit->side == BorderSide::Min) ? DragMode::MinBorder : DragMode::MaxBorder;
            m_dragged_range_index      = border_hit->range_index;
            m_drag_initial_mouse_y     = io.MousePos.y;
            m_drag_initial_range_min_z = static_cast<float>(range.min_z);
            m_drag_initial_range_max_z = static_cast<float>(range.max_z);
        } else {
            const std::optional<size_t> range_hit =
                this->pick_height_range(io.MousePos.y, profile_area_position, profile_area_size);
            if (range_hit.has_value()) {
                const HeightRangeEntry& range = this->height_ranges()[range_hit.value()];
                m_callbacks.height_range_clicked(range_hit.value());
                m_drag_mode                = DragMode::Body;
                m_dragged_range_index      = range_hit.value();
                m_drag_initial_mouse_y     = io.MousePos.y;
                m_drag_initial_range_min_z = static_cast<float>(range.min_z);
                m_drag_initial_range_max_z = static_cast<float>(range.max_z);
            } else {
                m_callbacks.height_range_clicked(std::nullopt);
            }
        }
    }

    if (active && m_drag_mode != DragMode::None) {
        if (m_drag_mode == DragMode::MinBorder || m_drag_mode == DragMode::MaxBorder) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        const ZRange dragged_height_range = this->compute_dragged_height_range(
            io.MousePos.y,
            profile_area_position,
            profile_area_size
        );
        m_callbacks.height_range_dragging(
            m_dragged_range_index,
            dragged_height_range.min_z,
            dragged_height_range.max_z
        );
    }

    if (m_was_active && !active && m_drag_mode != DragMode::None) {
        const ZRange final_range = this->compute_dragged_height_range(
            io.MousePos.y,
            profile_area_position,
            profile_area_size
        );
        m_callbacks
            .height_range_drag_ended(m_dragged_range_index, final_range.min_z, final_range.max_z);
        m_drag_mode = DragMode::None;
    }

    m_was_active  = active;
    m_was_hovered = hovered;

    ImGui::PopID();
}

std::optional<HeightRangeControl::BorderHitResult> HeightRangeControl::pick_height_range_border(
    const float mouse_y,
    const Vec2f& pos,
    const Vec2f& size
) const
{
    const HeightRangeEntries& height_ranges = this->height_ranges();
    if (height_ranges.empty() || object_max_z() <= 0.f) {
        return std::nullopt;
    }

    const auto border_distance = [&](const size_t i, const BorderSide side) -> float
    {
        const HeightRangeEntry& height_range = height_ranges[i];
        const float border_z = (side == BorderSide::Min) ? static_cast<float>(height_range.min_z) :
                                                           static_cast<float>(height_range.max_z);
        const float border_pos_y = this->project_layer_z(border_z, pos.y(), pos.y() + size.y());
        return std::abs(mouse_y - border_pos_y);
    };

    std::optional<BorderHitResult> nearest;

    const auto try_update_nearest = [&](const size_t i, const BorderSide side)
    {
        const float dist = border_distance(i, side);
        if (dist <= HEIGHT_RANGE_BORDER_DETECTION_THRESHOLD
            && (!nearest.has_value() || dist < nearest->distance))
        {
            nearest = BorderHitResult{i, side, dist};
        }
    };

    if (this->selected_range_index().has_value()) {
        ASSERT(this->selected_range_index().value() < height_ranges.size());
        const size_t selected_range_index = this->selected_range_index().value();

        try_update_nearest(selected_range_index, BorderSide::Min);
        try_update_nearest(selected_range_index, BorderSide::Max);
        if (nearest.has_value()) {
            return nearest;
        }
    }

    for (const HeightRangeEntry& height_range : height_ranges | std::views::reverse) {
        const size_t range_idx = &height_range - height_ranges.data();
        if (this->selected_range_index() == range_idx) {
            continue;
        }

        try_update_nearest(range_idx, BorderSide::Min);
        try_update_nearest(range_idx, BorderSide::Max);
    }

    return nearest;
}

} // namespace Slic3r::App::Yoga
