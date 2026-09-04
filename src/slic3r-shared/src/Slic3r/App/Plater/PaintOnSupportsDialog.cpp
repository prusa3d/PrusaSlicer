#include "Slic3r/App/Plater/PaintOnSupportsDialog.hpp"

#include "Slic3r/App/Plater/PaintOnSupportsGizmo.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Algorithms;

namespace Slic3r::App::Plater {

PaintOnSupportsDialog::Callbacks& PaintOnSupportsDialog::callbacks()
{
    return m_callbacks;
}

PaintOnSupportsDialog::PaintOnSupportsDialog() : GizmoWindow()
{
    content()->set_padding(20.f);
    content()->set_gap(2.f * gap_size());

    revert_button()->set_visible(true);
    revert_button()->callbacks().action = [this]()
    {
        if (m_callbacks.painting_reset) {
            m_callbacks.painting_reset();
        }
    };
    revert_button()->set_tooltip(_u8L("Remove all selection"));

    std::unique_ptr<Item> tool_type_buttons = std::make_unique<Item>();
    tool_type_buttons->set_gap(5.f);

    m_brush_button      = add_icon_button(tool_type_buttons.get(), Render::Icon::PaintBrush);
    m_smart_fill_button = add_icon_button(tool_type_buttons.get(), Render::Icon::WandMagicSparkles);

    this->add_new_row(_u8L("Tool"), std::move(tool_type_buttons));
    m_tool_type_group.set_buttons({m_brush_button, m_smart_fill_button});
    m_tool_type_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton* last_checked)
    {
        if (current_checked == m_brush_button) {
            m_selected_tool_type = PaintOnGizmoBase::ToolType::BRUSH;
        } else if (current_checked == m_smart_fill_button) {
            m_selected_tool_type = PaintOnGizmoBase::ToolType::SMART_FILL;
        } else {
            ASSERT(false);
            return;
        }

        this->update_visibility();
        m_callbacks.tool_type_changed(m_selected_tool_type);
    };

    std::unique_ptr<Item> brush_shape_buttons = std::make_unique<Item>();
    brush_shape_buttons->set_gap(5.f);

    m_sphere_brush_button   = add_icon_button(brush_shape_buttons.get(), Render::Icon::Sphere);
    m_circle_brush_button   = add_icon_button(brush_shape_buttons.get(), Render::Icon::Circle);
    m_triangle_brush_button = add_icon_button(brush_shape_buttons.get(), Render::Icon::Triangle);

    m_brush_shape_row = this->add_new_row(_u8L("Shape"), std::move(brush_shape_buttons));
    m_brush_shape_group.set_buttons(
        {m_sphere_brush_button, m_circle_brush_button, m_triangle_brush_button}
    );
    m_brush_shape_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton* last_checked)
    {
        if (current_checked == m_sphere_brush_button) {
            m_selected_brush_type = TriangleSelector::CursorType::SPHERE;
        } else if (current_checked == m_circle_brush_button) {
            m_selected_brush_type = TriangleSelector::CursorType::CIRCLE;
        } else if (current_checked == m_triangle_brush_button) {
            m_selected_brush_type = TriangleSelector::CursorType::POINTER;
        } else {
            ASSERT(false);
        }

        this->update_visibility();
        m_callbacks.brush_shape_changed(m_selected_brush_type);
    };

    m_brush_radius_row =
        add_row_with_slider(content(), &m_brush_radius_slider, _u8L("Brush size"), _u8L("mm"))
            ->parent_item();
    m_brush_radius_slider->set_begin_value(PaintOnSupportsGizmo::CursorRadiusMin);
    m_brush_radius_slider->set_end_value(PaintOnSupportsGizmo::CursorRadiusMax);
    m_brush_radius_slider->set_step(0.01);
    m_brush_radius_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.brush_radius_changed(value); };

    m_smart_fill_angle_row =
        this->add_row_with_slider(content(), &m_smart_fill_angle_slider, _u8L("Smart fill angle"))
            ->parent_item();
    m_smart_fill_angle_slider->set_begin_value(PaintOnSupportsGizmo::SmartFillAngleMin);
    m_smart_fill_angle_slider->set_end_value(PaintOnSupportsGizmo::SmartFillAngleMax);
    m_smart_fill_angle_slider->set_step(PaintOnSupportsGizmo::SmartFillAngleStep);
    m_smart_fill_angle_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.smart_fill_angle_changed(value); };

    this->add_separator(this->content());

    Item* clipping_row = add_row_with_slider(
        content(),
        &m_clipping_of_view_slider,
        _u8L("Clipping of view"),
        std::string(),
        std::string()
    );
    clipping_row->set_gap(gap_size());
    m_clipping_of_view_slider->set_begin_value(0.);
    m_clipping_of_view_slider->set_end_value(1.);
    m_clipping_of_view_slider->set_step(0.01);
    m_clipping_of_view_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.clipping_of_view_value_changed(value); };

    m_clipping_of_view_reset_direction_button = clipping_row->emplace<LayoutButton>(
        0,
        std::string(),
        Render::Icon::ArrowUpToLine,
        _u8L("Reset clipping direction")
    );
    m_clipping_of_view_reset_direction_button->callbacks().action = [this]()
    { m_callbacks.clipping_of_view_reset_direction(); };

    this->add_separator(this->content());

    add_row_with_slider(
        content(),
        &m_highlight_overhangs_angle_slider,
        _u8L("Show overhangs"),
        _u8L("°"),
        std::string()
    );
    m_highlight_overhangs_angle_slider->set_validator(std::make_unique<IntValidator>(0, 90));
    m_highlight_overhangs_angle_slider->set_step(1.);
    m_highlight_overhangs_angle_slider->callbacks().value_changed = [this](double value)
    {
        m_callbacks.highlight_overhangs_angle_changed(value);

        if (value <= 0.) {
            m_overhangs_enforce_button->set_enabled(false);
        } else if (value > 0. && !m_overhangs_enforce_button->enabled()) {
            m_overhangs_enforce_button->set_enabled(true);
        }
    };

    add_row_with_button(content(), &m_overhangs_enforce_button, _u8L("Enforce"));
    m_overhangs_enforce_button->callbacks().action = [this]()
    {
        m_callbacks.overhangs_enforced();
        this->set_highlight_overhangs_angle(0.);
    };

    this->add_separator(this->content());

    m_paint_on_overhangs_only_toggle =
        this->content()->emplace_back<ToggleButton>(_u8L("Paint on overhangs only"));
    m_paint_on_overhangs_only_toggle->set_flex_shrink(0);
    m_paint_on_overhangs_only_toggle->callbacks().checked_changed = [this](bool checked)
    { m_callbacks.paint_on_overhangs_only_value_changed(checked); };

    m_split_triangles_toggle = this->content()->emplace_back<ToggleButton>(_u8L("Split triangles"));
    m_split_triangles_toggle->set_flex_shrink(0);
    m_split_triangles_toggle->callbacks().checked_changed = [this](bool checked)
    { m_callbacks.split_triangles_value_changed(checked); };

    this->add_separator(this->content());

    add_row_with_button(content(), &m_automatic_painting_button, _u8L("Automatic painting"));
    m_automatic_painting_button->callbacks().action = [this]()
    { m_callbacks.automatic_painting(); };

    this->add_separator(this->content());

    m_help_factory.init(add_non_shrinked_wrap(content(), Orientation::Vertical, gap_size()));
    m_help_factory.add_item({GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}}, _u8L("Paint"));
    m_help_factory.add_item({GizmoHelpFactory::HelpIcon{Render::Icon::MouseRight}}, _u8L("Block"));
    m_help_factory.add_item(
        {{"SHIFT"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Remove")
    );
}

void PaintOnSupportsDialog::set_brush_radius(const double brush_radius)
{
    m_brush_radius_slider->set_value(brush_radius);
}

void PaintOnSupportsDialog::set_clipping_of_view_value(const double clipping_of_view_value)
{
    m_clipping_of_view_slider->set_value(clipping_of_view_value);
}

void PaintOnSupportsDialog::set_highlight_overhangs_angle(const double highlight_overhangs_angle)
{
    m_highlight_overhangs_angle_slider->set_value(highlight_overhangs_angle);

    if (highlight_overhangs_angle <= 0.) {
        m_overhangs_enforce_button->set_enabled(false);
    } else if (highlight_overhangs_angle > 0. && !m_overhangs_enforce_button->enabled()) {
        m_overhangs_enforce_button->set_enabled(true);
    }
}

void PaintOnSupportsDialog::set_smart_fill_angle(const double smart_fill_angle)
{
    m_smart_fill_angle_slider->set_value(smart_fill_angle);
}

void PaintOnSupportsDialog::set_paint_on_overhangs_only_value(const bool paint_on_overhangs_only)
{
    m_paint_on_overhangs_only_toggle->set_checked(paint_on_overhangs_only);
}

void PaintOnSupportsDialog::set_split_triangles_value(const bool split_triangles)
{
    m_split_triangles_toggle->set_checked(split_triangles);
}

void PaintOnSupportsDialog::set_tool_type(const PaintOnGizmoBase::ToolType& tool_type)
{
    m_selected_tool_type = tool_type;

    switch (tool_type) {
    case PaintOnGizmoBase::ToolType::BRUSH:
        m_brush_button->set_checked(true);
        break;
    case PaintOnGizmoBase::ToolType::SMART_FILL:
        m_smart_fill_button->set_checked(true);
        break;
    default:
        ASSERT(false);
        break;
    }

    this->update_visibility();
}

void PaintOnSupportsDialog::set_brush_type(
    const Biz::Algorithms::TriangleSelector::CursorType& brush_type
)
{
    m_selected_brush_type = brush_type;

    switch (brush_type) {
    case TriangleSelector::CursorType::SPHERE:
        m_sphere_brush_button->set_checked(true);
        break;
    case TriangleSelector::CursorType::CIRCLE:
        m_circle_brush_button->set_checked(true);
        break;
    case TriangleSelector::CursorType::POINTER:
        m_triangle_brush_button->set_checked(true);
        break;
    default:
        ASSERT(false);
        break;
    }

    this->update_visibility();
}

void PaintOnSupportsDialog::set_automatic_painting_running(const bool running)
{
    m_automatic_painting_button->set_enabled(!running);
    m_automatic_painting_button->set_label(
        running ? _u8L("Painting...") : _u8L("Automatic painting")
    );
}

void PaintOnSupportsDialog::update_visibility()
{
    if (m_selected_tool_type == PaintOnGizmoBase::ToolType::BRUSH) {
        m_brush_shape_row->set_visible(true);

        if (m_selected_brush_type == TriangleSelector::CursorType::POINTER) {
            m_brush_radius_row->set_visible(false);
            m_split_triangles_toggle->set_visible(false);
        } else {
            m_brush_radius_row->set_visible(true);
            m_split_triangles_toggle->set_visible(true);
        }

        m_smart_fill_angle_row->set_visible(false);
    } else if (m_selected_tool_type == PaintOnGizmoBase::ToolType::SMART_FILL) {
        m_brush_shape_row->set_visible(false);
        m_brush_radius_row->set_visible(false);
        m_split_triangles_toggle->set_visible(false);

        m_smart_fill_angle_row->set_visible(true);
    }
}

} // namespace Slic3r::App::Plater
