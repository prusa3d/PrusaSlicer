#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"

#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

// Lower values don't make sense, zero causes issues inside the algorithms.
static constexpr int DensityMin = 10;
static constexpr int DensityMax = 200;

static constexpr double HeadDiameterMin = 0.1;

SlaSupportPointsDialog::Callbacks& SlaSupportPointsDialog::callbacks()
{
    return m_callbacks;
}

SlaSupportPointsDialog::SlaSupportPointsDialog() : GizmoWindow()
{
    content()->set_padding(20.f);
    content()->set_gap(2.f * gap_size());

    m_status_text = content()->emplace_back<Text>(std::string{});
    m_status_text->set_wrap_mode(Text::WrapMode::Wrap);
    m_status_text->set_flex_shrink(0);

    add_row_with_button(
        content(),
        &m_auto_generate_button,
        _u8L("Auto-generate points"),
        _u8L("Generate new support points, discarding the current ones.")
    );
    m_auto_generate_button->callbacks().action = [this]() { m_callbacks.auto_generate(); };

    add_row_with_spin_int(
        _u8L("Density"),
        content(),
        &m_density_input,
        _u8L("%"),
        std::string(),
        DensityMin,
        DensityMax
    );
    m_density_input->callbacks().text_edited = [this]()
    {
        const int value = dynamic_cast<IntValidator*>(m_density_input->validator())->value();
        m_callbacks.density_changed(value);
    };

    add_row_with_button(content(), &m_remove_all_button, _u8L("Remove all points"));
    m_remove_all_button->callbacks().action = [this]() { m_callbacks.remove_all(); };

    this->add_separator(this->content());

    add_row_with_spin_double(
        _u8L("Head diameter"),
        content(),
        &m_head_diameter_input,
        _u8L("mm"),
        std::string(),
        HeadDiameterMin,
        HeadDiameterMin,
        0.1,
        0.5
    );
    m_head_diameter_input->callbacks().text_edited = [this]()
    {
        const double value =
            dynamic_cast<DoubleValidator*>(m_head_diameter_input->validator())->value();
        m_callbacks.head_diameter_changed(value);
    };

    m_lock_islands_toggle =
        content()->emplace_back<ToggleButton>(_u8L("Lock supports under new islands"));
    m_lock_islands_toggle->set_flex_shrink(0);
    m_lock_islands_toggle->callbacks().checked_changed = [this](bool checked)
    { m_callbacks.lock_islands_changed(checked); };

    add_row_with_button(
        content(),
        &m_apply_button,
        _u8L("Apply"),
        _u8L("Send the edited points to slicing. Closing the tool applies them too.")
    );
    m_apply_button->callbacks().action = [this]() { m_callbacks.apply(); };

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

    using HelpIcon = GizmoHelpFactory::HelpIcon;
    m_help_factory.init(add_non_shrinked_wrap(content(), Orientation::Vertical, gap_size()));
    m_help_factory.add_item({HelpIcon{Render::Icon::MouseLeft}}, _u8L("Add point"));
    m_help_factory.add_item({HelpIcon{Render::Icon::MouseRight}}, _u8L("Remove point"));
    m_help_factory.add_item({{_u8L("Drag")}}, _u8L("Move point"));
    m_help_factory.add_item(
        {{"SHIFT"}, HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Add point to selection / select by rectangle")
    );
    m_help_factory.add_item(
        {{"ALT"}, HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Remove point from selection / deselect by rectangle")
    );
    m_help_factory.add_item({{"CTRL"}, {"A"}}, _u8L("Select all points"));
    m_help_factory.add_item({{"DEL"}}, _u8L("Remove selected points"));
    m_help_factory.add_item({{"ENTER"}}, _u8L("Apply"));

    this->set_input_enabled(false);
}

void SlaSupportPointsDialog::set_status(const std::string& status)
{
    m_status_text->set_text(status);
}

void SlaSupportPointsDialog::set_input_enabled(const bool enabled)
{
    m_input_enabled = enabled;

    m_auto_generate_button->set_enabled(enabled);
    m_density_input->set_enabled(enabled);
    m_head_diameter_input->set_enabled(enabled);
    m_lock_islands_toggle->set_enabled(enabled);
    m_remove_all_button->set_enabled(enabled && m_remove_enabled);
    m_apply_button->set_enabled(enabled && m_apply_enabled);
}

void SlaSupportPointsDialog::set_apply_enabled(const bool enabled)
{
    m_apply_enabled = enabled;
    m_apply_button->set_enabled(m_input_enabled && enabled);
}

void SlaSupportPointsDialog::set_remove_all_enabled(const bool enabled)
{
    m_remove_enabled = enabled;
    m_remove_all_button->set_enabled(m_input_enabled && enabled);
}

void SlaSupportPointsDialog::set_density(const int density)
{
    m_density_input->set_text(std::to_string(density));
}

void SlaSupportPointsDialog::set_head_diameter(
    const double head_diameter,
    const double max_head_diameter
)
{
    auto* validator = dynamic_cast<DoubleValidator*>(m_head_diameter_input->validator());
    validator->set_to(std::max(HeadDiameterMin, max_head_diameter));
    m_head_diameter_input->set_text(fmt::format("{:.2g}", head_diameter));
}

void SlaSupportPointsDialog::set_lock_islands(const bool lock_islands)
{
    m_lock_islands_toggle->set_checked(lock_islands);
}

void SlaSupportPointsDialog::set_clipping_of_view_value(const double value)
{
    m_clipping_of_view_slider->set_value(value);
}

} // namespace Slic3r::App::Plater
