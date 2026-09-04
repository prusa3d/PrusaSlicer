#pragma once

#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"

namespace Slic3r::App::Yoga {
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class PaintOnSupportsDialog : public GizmoWindow
{
public:
    PaintOnSupportsDialog();

    struct Callbacks
    {
        std::function<void(PaintOnGizmoBase::ToolType)> tool_type_changed =
            [](PaintOnGizmoBase::ToolType) {};
        std::function<void(Biz::Algorithms::TriangleSelector::CursorType)> brush_shape_changed =
            [](Biz::Algorithms::TriangleSelector::CursorType) {};
        std::function<void(double)> brush_radius_changed                = [](double) {};
        std::function<void(double)> smart_fill_angle_changed            = [](double) {};
        std::function<void(double)> clipping_of_view_value_changed      = [](double) {};
        std::function<void()> clipping_of_view_reset_direction          = []() {};
        std::function<void(double)> highlight_overhangs_angle_changed   = [](double) {};
        std::function<void()> overhangs_enforced                        = []() {};
        std::function<void(bool)> paint_on_overhangs_only_value_changed = [](bool) {};
        std::function<void(bool)> split_triangles_value_changed         = [](bool) {};
        std::function<void()> automatic_painting                        = []() {};
        std::function<void()> painting_reset                            = []() {};
    };

    Callbacks& callbacks();

    void set_brush_radius(double brush_radius);

    void set_clipping_of_view_value(double clipping_of_view_value);

    void set_highlight_overhangs_angle(double highlight_overhangs_angle);

    void set_smart_fill_angle(double smart_fill_angle);

    void set_paint_on_overhangs_only_value(bool paint_on_overhangs_only);

    void set_split_triangles_value(bool split_triangles);

    void set_tool_type(const PaintOnGizmoBase::ToolType& tool_type);

    void set_brush_type(const Biz::Algorithms::TriangleSelector::CursorType& brush_type);

    void set_automatic_painting_running(bool running);

private:
    void update_visibility();

    PaintOnGizmoBase::ToolType m_selected_tool_type = PaintOnGizmoBase::ToolType::BRUSH;
    Biz::Algorithms::TriangleSelector::CursorType m_selected_brush_type =
        Biz::Algorithms::TriangleSelector::CursorType::SPHERE;

    Yoga::LayoutButton* m_brush_button      = nullptr;
    Yoga::LayoutButton* m_smart_fill_button = nullptr;
    Yoga::ButtonGroup m_tool_type_group;

    Yoga::LayoutButton* m_sphere_brush_button   = nullptr;
    Yoga::LayoutButton* m_circle_brush_button   = nullptr;
    Yoga::LayoutButton* m_triangle_brush_button = nullptr;
    Yoga::ButtonGroup m_brush_shape_group;

    Yoga::SliderWithInput* m_brush_radius_slider = nullptr;
    Yoga::SliderWithInput* m_smart_fill_angle_slider;

    Yoga::SliderWithInput* m_clipping_of_view_slider              = nullptr;
    Yoga::LayoutButton* m_clipping_of_view_reset_direction_button = nullptr;

    Yoga::SliderWithInput* m_highlight_overhangs_angle_slider = nullptr;
    Yoga::LayoutButton* m_overhangs_enforce_button            = nullptr;

    Yoga::ToggleButton* m_paint_on_overhangs_only_toggle = nullptr;
    Yoga::ToggleButton* m_split_triangles_toggle         = nullptr;

    Yoga::LayoutButton* m_automatic_painting_button = nullptr;

    Yoga::Item* m_brush_shape_row      = nullptr;
    Yoga::Item* m_brush_radius_row     = nullptr;
    Yoga::Item* m_smart_fill_angle_row = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater
