#pragma once

#include "Slic3r/App/Plater/LayerHeightProfileControl.hpp"

#include <functional>
#include <optional>

namespace Slic3r::App::Plater {

class HeightRangeControl : public LayerHeightProfileControl
{
public:
    struct Callbacks
    {
        std::function<void(std::optional<size_t> range_index)> height_range_clicked =
            [](std::optional<size_t>) {};
        std::function<void(size_t range_index, double new_min_z, double new_max_z)>
            height_range_dragging = [](size_t, double, double) {};
        std::function<void(std::optional<size_t> range_index)> height_range_hovered =
            [](std::optional<size_t>) {};
        std::function<void(size_t range_index, double final_min_z, double final_max_z)>
            height_range_drag_ended = [](size_t, double, double) {};
    };

    HeightRangeControl();

    Callbacks& callbacks();

    void render(const Domain::Vec2f& pos, const Domain::Vec2f& size) override;

private:
    enum class BorderSide
    {
        Min,
        Max
    };

    struct ZRange
    {
        float min_z{0.f};
        float max_z{0.f};
    };

    enum class DragMode
    {
        None,
        MinBorder,
        MaxBorder,
        Body
    };

    struct BorderHitResult
    {
        size_t range_index{0};
        BorderSide side{BorderSide::Min};
        float distance{0.f};
    };

    void process_input(
        const Domain::Vec2f& pos,
        const Domain::Vec2f& size,
        const Domain::Vec2f& profile_area_position,
        const Domain::Vec2f& profile_area_size
    );
    ZRange compute_dragged_height_range(
        float mouse_y,
        const Domain::Vec2f& pos,
        const Domain::Vec2f& size
    ) const;
    std::optional<BorderHitResult> pick_height_range_border(
        float mouse_y,
        const Domain::Vec2f& pos,
        const Domain::Vec2f& size
    ) const;

    DragMode m_drag_mode = DragMode::None;
    size_t m_dragged_range_index{0};
    float m_drag_initial_mouse_y{0.f};
    float m_drag_initial_range_min_z{0.f};
    float m_drag_initial_range_max_z{0.f};
    bool m_was_active{false};
    bool m_was_hovered{false};

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater
