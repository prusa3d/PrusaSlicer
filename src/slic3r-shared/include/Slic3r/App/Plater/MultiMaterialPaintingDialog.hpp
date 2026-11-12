#pragma once

#include "Slic3r/App/Plater/MMPaintingUtils.hpp"
#include "Slic3r/App/Plater/PaintOnGizmoBase.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/Biz/Algorithms/TriangleSelector.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

namespace Slic3r::App::Plater {

class ColorSelector;
class ColorDropdowns;

class MultiMaterialPaintingDialog : public GizmoWindow
{
public:
    explicit MultiMaterialPaintingDialog(Biz::ProjectInteractor& project_interactor);

    struct Callbacks
    {
        std::function<void(size_t)> first_brush_color_changed  = [](size_t) {};
        std::function<void(size_t)> second_brush_color_changed = [](size_t) {};
        std::function<void(PaintOnGizmoBase::ToolType)> tool_type_changed =
            [](PaintOnGizmoBase::ToolType) {};
        std::function<void(Biz::Algorithms::TriangleSelector::CursorType)> brush_shape_changed =
            [](Biz::Algorithms::TriangleSelector::CursorType) {};
        std::function<void(double)> brush_radius_changed           = [](double) {};
        std::function<void(double)> smart_fill_angle_changed       = [](double) {};
        std::function<void(double)> bucket_fill_angle_changed      = [](double) {};
        std::function<void(double)> height_range_changed           = [](double) {};
        std::function<void(bool)> split_triangles_value_changed    = [](bool) {};
        std::function<void(double)> clipping_of_view_value_changed = [](double) {};
        std::function<void()> clipping_of_view_reset_direction     = []() {};
        std::function<void()> painting_reset                       = []() {};
    };

    Callbacks& callbacks();

    void set_brush_type(const Biz::Algorithms::TriangleSelector::CursorType& brush_type);

    void set_brush_radius(double brush_radius);

    void set_tool_type(const PaintOnGizmoBase::ToolType& tool_type);

    void set_split_triangles_value(bool split_triangles);

    void set_smart_fill_angle(double smart_fill_angle);

    void set_bucket_fill_angle(double bucket_fill_angle);

    void set_height_range(double height_range);

    void set_clipping_of_view_value(double clipping_of_view_value);

    void set_first_brush_color_index(size_t color_idx);

    void set_second_brush_color_index(size_t color_idx);

    void set_painting_colors(const PaintingPalette& palette);

    void switch_colors();

private:
    void update_visibility();
    Yoga::ItemPtr brush_properties_picker();
    Yoga::ItemPtr brush_size_picker();
    Yoga::ItemPtr smart_fill_angle_picker();
    Yoga::ItemPtr bucket_fill_angle_picker();
    Yoga::ItemPtr height_range_picker();

    PaintOnGizmoBase::ToolType m_selected_tool_type = PaintOnGizmoBase::ToolType::BRUSH;
    Biz::Algorithms::TriangleSelector::CursorType m_selected_brush_type =
        Biz::Algorithms::TriangleSelector::CursorType::SPHERE;

    ColorSelector* m_color_selector{nullptr};
    ColorDropdowns* m_color_dropdowns{nullptr};

    Yoga::LayoutButton* m_brush_button         = nullptr;
    Yoga::LayoutButton* m_smart_fill_button    = nullptr;
    Yoga::LayoutButton* m_bucket_fill_button   = nullptr;
    Yoga::LayoutButton* m_color_replace_button = nullptr;
    Yoga::LayoutButton* m_height_range_button  = nullptr;
    Yoga::ButtonGroup m_tool_type_group;

    Yoga::LayoutButton* m_sphere_brush_button   = nullptr;
    Yoga::LayoutButton* m_circle_brush_button   = nullptr;
    Yoga::LayoutButton* m_triangle_brush_button = nullptr;
    Yoga::ButtonGroup m_brush_shape_group;

    std::vector<Yoga::LayoutButton*> m_first_brush_color_buttons;
    std::vector<Yoga::LayoutButton*> m_second_brush_color_buttons;
    Yoga::ButtonGroup m_first_brush_color_group;
    Yoga::ButtonGroup m_second_brush_color_group;

    Yoga::SliderWithInput* m_brush_radius_slider;
    Yoga::SliderWithInput* m_smart_fill_angle_slider;
    Yoga::SliderWithInput* m_bucket_fill_angle_slider;
    Yoga::SliderWithInput* m_height_range_slider;
    Yoga::SliderWithInput* m_clipping_of_view_slider;

    Yoga::ToggleButton* m_split_triangles_toggle = nullptr;

    Yoga::LayoutButton* m_clipping_of_view_reset_direction_button = nullptr;
    Yoga::LayoutButton* m_painting_reset_button                   = nullptr;

    Yoga::Item* m_shapes_label                   = nullptr;
    Yoga::Item* m_brush_shape_row                = nullptr;
    Yoga::Item* m_brush_radius_row               = nullptr;
    Yoga::Item* m_smart_fill_angle_row           = nullptr;
    Yoga::Item* m_bucket_fill_angle_row          = nullptr;
    Yoga::Item* m_height_range_row               = nullptr;
    Yoga::Item* m_split_triangles_section        = nullptr;
    Yoga::Separator* m_split_triangles_separator = nullptr;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::Plater
