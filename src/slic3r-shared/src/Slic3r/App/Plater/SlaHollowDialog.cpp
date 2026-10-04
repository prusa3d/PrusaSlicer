#include "Slic3r/App/Plater/SlaHollowDialog.hpp"

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

// The ranges of the hollowing options (ConfigDefsSLA.cpp).
static constexpr double WallThicknessMin   = 1.;
static constexpr double WallThicknessMax   = 10.;
static constexpr double AccuracyMin        = 0.;
static constexpr double AccuracyMax        = 1.;
static constexpr double ClosingDistanceMin = 0.;
static constexpr double ClosingDistanceMax = 10.;

// As in the old gizmo: a slider range, but larger values can be typed in.
static constexpr double HoleDiameterMin = 0.1;
static constexpr double HoleDiameterMax = 60.;
static constexpr double HoleDepthMin    = 0.;
static constexpr double HoleDepthMax    = 100.;

static double validated_value(InputTextWithSpin* input)
{
    return dynamic_cast<DoubleValidator*>(input->validator())->value();
}

SlaHollowDialog::Callbacks& SlaHollowDialog::callbacks()
{
    return m_callbacks;
}

SlaHollowDialog::SlaHollowDialog() : GizmoWindow()
{
    content()->set_padding(20.f);
    content()->set_gap(2.f * gap_size());

    m_status_text = content()->emplace_back<Text>(std::string{});
    m_status_text->set_wrap_mode(Text::WrapMode::Wrap);
    m_status_text->set_flex_shrink(0);

    m_hollowing_toggle = content()->emplace_back<ToggleButton>(_u8L("Hollow this object"));
    m_hollowing_toggle->set_flex_shrink(0);
    m_hollowing_toggle->callbacks().checked_changed = [this](bool checked)
    {
        m_hollowing_enabled = checked;
        this->update_enabled();
        // set_checked() calls back too, only the user's changes go to the gizmo.
        if (!m_setting_values) {
            m_callbacks.hollowing_enabled_changed(checked);
        }
    };

    add_row_with_spin_double(
        _u8L("Wall thickness"),
        content(),
        &m_wall_thickness_input,
        _u8L("mm"),
        std::string(),
        WallThicknessMin,
        WallThicknessMax,
        0.5,
        1.
    );
    m_wall_thickness_input->callbacks().text_edited = [this]()
    { m_callbacks.wall_thickness_changed(validated_value(m_wall_thickness_input)); };

    add_row_with_spin_double(
        _u8L("Accuracy"),
        content(),
        &m_accuracy_input,
        std::string(),
        std::string(),
        AccuracyMin,
        AccuracyMax,
        0.1,
        0.2
    );
    m_accuracy_input->callbacks().text_edited = [this]()
    { m_callbacks.accuracy_changed(validated_value(m_accuracy_input)); };

    add_row_with_spin_double(
        _u8L("Closing distance"),
        content(),
        &m_closing_distance_input,
        _u8L("mm"),
        std::string(),
        ClosingDistanceMin,
        ClosingDistanceMax,
        0.5,
        1.
    );
    m_closing_distance_input->callbacks().text_edited = [this]()
    { m_callbacks.closing_distance_changed(validated_value(m_closing_distance_input)); };

    this->add_separator(this->content());

    add_row_with_spin_double(
        _u8L("Hole diameter"),
        content(),
        &m_hole_diameter_input,
        _u8L("mm"),
        std::string(),
        HoleDiameterMin,
        HoleDiameterMax,
        0.5,
        2.
    );
    m_hole_diameter_input->callbacks().text_edited = [this]()
    { m_callbacks.hole_diameter_changed(validated_value(m_hole_diameter_input)); };

    add_row_with_spin_double(
        _u8L("Hole depth"),
        content(),
        &m_hole_depth_input,
        _u8L("mm"),
        std::string(),
        HoleDepthMin,
        HoleDepthMax,
        0.5,
        2.
    );
    m_hole_depth_input->callbacks().text_edited = [this]()
    { m_callbacks.hole_depth_changed(validated_value(m_hole_depth_input)); };

    add_row_with_button(content(), &m_remove_all_button, _u8L("Remove all holes"));
    m_remove_all_button->callbacks().action = [this]() { m_callbacks.remove_all(); };

    add_row_with_button(
        content(),
        &m_apply_button,
        _u8L("Apply"),
        _u8L("Send the edited holes to slicing and show the drilled object. Closing the tool "
             "applies them too.")
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
    m_help_factory.add_item({HelpIcon{Render::Icon::MouseLeft}}, _u8L("Add drainage hole"));
    m_help_factory.add_item({HelpIcon{Render::Icon::MouseRight}}, _u8L("Remove drainage hole"));
    m_help_factory.add_item({{_u8L("Drag")}}, _u8L("Move drainage hole"));
    m_help_factory.add_item(
        {{"SHIFT"}, HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Add hole to selection / select by rectangle")
    );
    m_help_factory.add_item(
        {{"ALT"}, HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Remove hole from selection / deselect by rectangle")
    );
    m_help_factory.add_item({{"CTRL"}, {"A"}}, _u8L("Select all holes"));
    m_help_factory.add_item({{"DEL"}}, _u8L("Remove selected holes"));
    m_help_factory.add_item({{"ENTER"}}, _u8L("Apply"));

    this->set_input_enabled(false);
}

void SlaHollowDialog::set_status(const std::string& status)
{
    m_status_text->set_text(status);
}

void SlaHollowDialog::update_enabled()
{
    m_hollowing_toggle->set_enabled(m_input_enabled);
    m_wall_thickness_input->set_enabled(m_input_enabled && m_hollowing_enabled);
    m_accuracy_input->set_enabled(m_input_enabled && m_hollowing_enabled);
    m_closing_distance_input->set_enabled(m_input_enabled && m_hollowing_enabled);
    m_hole_diameter_input->set_enabled(m_input_enabled);
    m_hole_depth_input->set_enabled(m_input_enabled);
    m_remove_all_button->set_enabled(m_input_enabled && m_remove_enabled);
    m_apply_button->set_enabled(m_input_enabled && m_apply_enabled);
}

void SlaHollowDialog::set_input_enabled(const bool enabled)
{
    m_input_enabled = enabled;
    this->update_enabled();
}

void SlaHollowDialog::set_apply_enabled(const bool enabled)
{
    m_apply_enabled = enabled;
    this->update_enabled();
}

void SlaHollowDialog::set_remove_all_enabled(const bool enabled)
{
    m_remove_enabled = enabled;
    this->update_enabled();
}

void SlaHollowDialog::set_hollowing(
    const bool enabled,
    const double wall_thickness,
    const double accuracy,
    const double closing_distance
)
{
    m_hollowing_enabled = enabled;
    m_setting_values    = true;
    m_hollowing_toggle->set_checked(enabled);
    m_setting_values = false;
    m_wall_thickness_input->set_text(fmt::format("{:.2g}", wall_thickness));
    m_accuracy_input->set_text(fmt::format("{:.2g}", accuracy));
    m_closing_distance_input->set_text(fmt::format("{:.2g}", closing_distance));
    this->update_enabled();
}

void SlaHollowDialog::set_hole_size(const double diameter, const double depth)
{
    m_hole_diameter_input->set_text(fmt::format("{:.3g}", diameter));
    m_hole_depth_input->set_text(fmt::format("{:.3g}", depth));
}

void SlaHollowDialog::set_clipping_of_view_value(const double value)
{
    m_clipping_of_view_slider->set_value(value);
}

} // namespace Slic3r::App::Plater
