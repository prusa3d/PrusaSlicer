#pragma once

#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"

namespace Slic3r::App::Plater {

class PaintOnSeamsDialog : public GizmoWindow
{
public:
    PaintOnSeamsDialog();

    struct Callbacks
    {
        std::function<void(Biz::Algorithms::TriangleSelector::CursorType)> brush_shape_changed =
            [](Biz::Algorithms::TriangleSelector::CursorType) {};
        std::function<void(double)> brush_radius_changed           = [](double) {};
        std::function<void(double)> clipping_of_view_value_changed = [](double) {};
        std::function<void()> clipping_of_view_reset_direction     = []() {};
        std::function<void()> painting_reset                       = []() {};
    };

    Callbacks& callbacks();

    void set_brush_radius(double brush_radius);

    void set_clipping_of_view_value(double clipping_of_view_value);

    void set_brush_type(const Biz::Algorithms::TriangleSelector::CursorType& brush_type);

private:
    Yoga::LayoutButton* m_sphere_brush_button = nullptr;
    Yoga::LayoutButton* m_circle_brush_button = nullptr;
    Yoga::ButtonGroup m_brush_shape_group;

    Yoga::SliderWithInput* m_brush_radius_slider     = nullptr;
    Yoga::SliderWithInput* m_clipping_of_view_slider = nullptr;

    Yoga::LayoutButton* m_clipping_of_view_reset_direction_button = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater
