#include "Slic3r/App/Plater/VariableLayerHeightControl.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <cmath>
#include <fmt/format.h>
#include <imgui/imgui.h>
#include <numbers>

namespace Slic3r::App::Plater {

const constexpr float LAYER_HEIGHT_PROFILE_PADDING = 5.f;
const constexpr ImColor LAYER_HEIGHT_CURSOR_COLOR  = ImColor(255, 255, 0, 255);

VariableLayerHeightControl::VariableLayerHeightControl() : LayerHeightProfileControl()
{
    this->set_object_name("VariableLayerHeightControl");
}

VariableLayerHeightControl::Callbacks& VariableLayerHeightControl::callbacks()
{
    return m_callbacks;
}

void VariableLayerHeightControl::set_cursor_band_width(const float band_width)
{
    m_cursor_band_width = band_width;
}

void VariableLayerHeightControl::set_cursor_normalized_position(const float normalized_position)
{
    m_cursor_normalized_position = normalized_position;
}

void VariableLayerHeightControl::reset_cursor_position()
{
    m_cursor_normalized_position.reset();
}

void VariableLayerHeightControl::render(const Domain::Vec2f& pos, const Domain::Vec2f& size)
{
    if (size.x() <= 0.f || size.y() <= 0.f) {
        return;
    }

    this->render_item_begin(pos, size);

    const Domain::Vec2f profile_area_position{pos.x() + LAYER_HEIGHT_PROFILE_PADDING, pos.y()};
    const Domain::Vec2f profile_area_size{size.x() - 2.f * LAYER_HEIGHT_PROFILE_PADDING, size.y()};

    this->process_input(pos, size, profile_area_position, profile_area_size);

    this->render_baseline(profile_area_position, profile_area_size);
    this->render_height_ranges(profile_area_position, profile_area_size);
    this->render_layer_height_profile(profile_area_position, profile_area_size);
    this->render_cursor(profile_area_position, profile_area_size);
    this->render_height_range_tooltip();

    this->render_item_end(pos, size);
}

void VariableLayerHeightControl::render_cursor(const Domain::Vec2f& pos, const Domain::Vec2f& size) const
{
    if (!m_cursor_normalized_position.has_value() || this->object_max_z() <= 0.f) {
        return;
    }

    ASSERT(
        0.f <= m_cursor_normalized_position.value() && m_cursor_normalized_position.value() <= 1.f
    );

    const float cursor_normalized = (1.f - m_cursor_normalized_position.value());
    const float cursor_center_y   = pos.y() + cursor_normalized * size.y();
    const float band_height       = m_cursor_band_width * (size.y() / this->object_max_z());
    const float visible_top_y     = pos.y();
    const float visible_bottom_y  = pos.y() + size.y();

    // Early exit if the cursor completely outside visible area.
    if (cursor_center_y + band_height < visible_top_y
        || cursor_center_y - band_height > visible_bottom_y)
    {
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Use multiple segments for the smooth cosine gradient.
    constexpr int num_gradient_segments = 16;
    for (size_t i = 0; i < num_gradient_segments; ++i) {
        const float segment_start_normalized = static_cast<float>(i) / num_gradient_segments;
        const float segment_end_normalized   = static_cast<float>(i + 1) / num_gradient_segments;

        float segment_top_y =
            cursor_center_y - band_height + segment_start_normalized * band_height * 2.f;
        float segment_bottom_y =
            cursor_center_y - band_height + segment_end_normalized * band_height * 2.f;

        // Skip segments completely outside the visible area.
        if (segment_bottom_y < visible_top_y || segment_top_y > visible_bottom_y) {
            continue;
        }

        // Clamp segments to the visible area.
        segment_top_y    = std::max(segment_top_y, visible_top_y);
        segment_bottom_y = std::min(segment_bottom_y, visible_bottom_y);

        // Distance from the center: 0 = center, 1 = edge
        const float dist_from_center_start = std::abs(segment_start_normalized * 2.0f - 1.0f);
        const float dist_from_center_end   = std::abs(segment_end_normalized * 2.0f - 1.0f);

        // Cosine gradient matching shader formula.
        const float alpha_top = 0.25f
                * std::cos(std::min(
                    std::numbers::pi_v<float>,
                    dist_from_center_start * std::numbers::pi_v<float> * 1.8f
                ))
            + 0.25f;
        const float alpha_bottom = 0.25f
                * std::cos(std::min(
                    std::numbers::pi_v<float>,
                    dist_from_center_end * std::numbers::pi_v<float> * 1.8f
                ))
            + 0.25f;

        const ImColor color_top{
            LAYER_HEIGHT_CURSOR_COLOR.Value.x,
            LAYER_HEIGHT_CURSOR_COLOR.Value.y,
            LAYER_HEIGHT_CURSOR_COLOR.Value.z,
            alpha_top
        };
        const ImColor color_bottom{
            LAYER_HEIGHT_CURSOR_COLOR.Value.x,
            LAYER_HEIGHT_CURSOR_COLOR.Value.y,
            LAYER_HEIGHT_CURSOR_COLOR.Value.z,
            alpha_bottom
        };

        draw_list->AddRectFilledMultiColor(
            ImVec2(pos.x(), segment_top_y),
            ImVec2(pos.x() + size.x(), segment_bottom_y),
            color_top,
            color_top,
            color_bottom,
            color_bottom
        );
    }
}

void VariableLayerHeightControl::process_input(
    const Domain::Vec2f& pos,
    const Domain::Vec2f& size,
    const Domain::Vec2f& profile_area_position,
    const Domain::Vec2f& profile_area_size
)
{
    ImGui::SetCursorScreenPos(ImVec2(pos.x(), pos.y()));
    ImGui::PushID(object_name().c_str());

    // Create an invisible button covering the entire area.
    ImGui::InvisibleButton(
        "##layer_height_profile",
        ImVec2(size.x(), size.y()),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
    );

    const ImGuiIO& io = ImGui::GetIO();

    const bool is_left_button_clicked  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool is_right_button_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool hovered                 = ImGui::IsItemHovered();
    const bool active                  = ImGui::IsItemActive();
    const bool shift_down              = io.KeyShift;
    const bool ctrl_down               = io.KeyCtrl;
    const bool alt_down                = io.KeyAlt;

    const float cursor_normalized_position =
        std::clamp(1.f - (io.MousePos.y - pos.y()) / size.y(), 0.f, 1.f);

    if (hovered && !active) {
        const std::optional<size_t> picked_range_index =
            this->pick_height_range(io.MousePos.y, profile_area_position, profile_area_size);
        if (picked_range_index.has_value()) {
            this->set_hovered_height_range(picked_range_index.value());
        } else {
            this->reset_hovered_height_range();
        }
    } else if (!hovered) {
        this->reset_hovered_height_range();
    }

    if (is_left_button_clicked && alt_down) {
        const std::optional<size_t> picked_range_index =
            this->pick_height_range(io.MousePos.y, profile_area_position, profile_area_size);
        if (picked_range_index.has_value()) {
            m_callbacks.on_height_range_click();
            ImGui::PopID();
            return;
        }
    }

    if (hovered && !active) {
        m_callbacks.on_mouse_move(cursor_normalized_position);
    } else if (m_was_hovered && !hovered && !active) {
        // Mouse just left the layer height profile area.
        m_callbacks.on_mouse_move(std::nullopt);
    }

    if (is_left_button_clicked || is_right_button_clicked) {
        m_mouse_button_down = is_left_button_clicked ? Button::Left : Button::Right;
        m_callbacks
            .on_mouse_down(cursor_normalized_position, shift_down, ctrl_down, m_mouse_button_down);
    }

    if (active && (io.MouseDown[0] || io.MouseDown[1])) {
        m_callbacks
            .on_mouse_drag(cursor_normalized_position, shift_down, ctrl_down, m_mouse_button_down);
    }

    // Detect mouse up via active state transition.
    if (m_was_active && !active) {
        m_callbacks.on_mouse_up(m_mouse_button_down);
        m_mouse_button_down = Button::None;
    }

    if (hovered && io.MouseWheel != 0.f) {
        m_callbacks.on_mouse_wheel(io.MouseWheel, ctrl_down);
    }

    m_was_active  = active;
    m_was_hovered = hovered;

    ImGui::PopID();
}

void VariableLayerHeightControl::render_height_range_tooltip() const
{
    if (!this->hovered_range_index().has_value()) {
        return;
    }

    const size_t hovered_range_index     = this->hovered_range_index().value();
    const HeightRangeEntry& height_range = this->height_ranges()[hovered_range_index];
    const std::string tooltip_text       = fmt::format(
        fmt::runtime(Biz::_u8L("Layer height modifier: {:.2f} - {:.2f} mm")),
        height_range.min_z,
        height_range.max_z
    );

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
    ImGui::SetTooltip("%s", tooltip_text.c_str());
    ImGui::PopStyleVar();
}

} // namespace Slic3r::App::Yoga
