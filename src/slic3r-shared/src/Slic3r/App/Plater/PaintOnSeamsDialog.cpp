#include "Slic3r/App/Plater/PaintOnSeamsDialog.hpp"

#include "Slic3r/App/Plater/PaintOnSeamsGizmo.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

PaintOnSeamsDialog::Callbacks& PaintOnSeamsDialog::callbacks()
{
    return m_callbacks;
}

PaintOnSeamsDialog::PaintOnSeamsDialog() : GizmoWindow()
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

    std::unique_ptr<Item> brush_shape_buttons = std::make_unique<Item>();
    brush_shape_buttons->set_gap(5.f);

    m_sphere_brush_button = add_icon_button(brush_shape_buttons.get(), Render::Icon::Sphere);
    m_circle_brush_button = add_icon_button(brush_shape_buttons.get(), Render::Icon::Circle);

    add_new_row(_u8L("Shape"), std::move(brush_shape_buttons))->parent_item();
    m_brush_shape_group.set_buttons({m_sphere_brush_button, m_circle_brush_button});
    m_brush_shape_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton* last_checked)
    {
        if (current_checked == m_sphere_brush_button) {
            m_callbacks.brush_shape_changed(Biz::Algorithms::TriangleSelector::CursorType::SPHERE);
        } else if (current_checked == m_circle_brush_button) {
            m_callbacks.brush_shape_changed(Biz::Algorithms::TriangleSelector::CursorType::CIRCLE);
        } else {
            ASSERT(false);
        }
    };

    constexpr float slider_text_size = 50;

    add_row_with_slider(content(), &m_brush_radius_slider, _u8L("Brush size"), _u8L("mm"));
    m_brush_radius_slider->set_begin_value(PaintOnSeamsGizmo::CursorRadiusMin);
    m_brush_radius_slider->set_end_value(PaintOnSeamsGizmo::CursorRadiusMax);
    m_brush_radius_slider->set_step(0.01);
    m_brush_radius_slider->set_input_width(slider_text_size);
    m_brush_radius_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.brush_radius_changed(value); };

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

    m_help_factory.init(add_non_shrinked_wrap(content(), Orientation::Vertical, gap_size()));
    m_help_factory.add_item({GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}}, _u8L("Paint"));
    m_help_factory.add_item({GizmoHelpFactory::HelpIcon{Render::Icon::MouseRight}}, _u8L("Block"));
    m_help_factory.add_item(
        {{"SHIFT"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Remove")
    );
}

void PaintOnSeamsDialog::set_brush_radius(const double brush_radius)
{
    m_brush_radius_slider->set_value(brush_radius);
}

void PaintOnSeamsDialog::set_clipping_of_view_value(const double clipping_of_view_value)
{
    m_clipping_of_view_slider->set_value(clipping_of_view_value);
}

void PaintOnSeamsDialog::set_brush_type(
    const Biz::Algorithms::TriangleSelector::CursorType& brush_type
)
{
    using namespace Slic3r::Biz::Algorithms;

    switch (brush_type) {
    case TriangleSelector::CursorType::SPHERE:
        m_sphere_brush_button->set_checked(true);
        break;
    case TriangleSelector::CursorType::CIRCLE:
        m_circle_brush_button->set_checked(true);
        break;
    default:
        ASSERT(false);
        break;
    }
}

} // namespace Slic3r::App::Plater
