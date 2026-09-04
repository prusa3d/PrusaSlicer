#include "Slic3r/App/Plater/VariableLayerHeightDialog.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Slider.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Plater/VariableLayerHeightControl.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

using Slic3r::Domain::ZHeightPairs;

namespace Slic3r::App::Plater {

static const Margins BUTTON_TEXT_MARGIN = {10.f, 2.f, 10.f, 2.f};

VariableLayerHeightDialog::VariableLayerHeightDialog() : GizmoWindowWithLeftSidePanel()
{
    side_panel()->set_min_width(80);
    this->content()->set_orientation(Orientation::Vertical);
    this->content()->set_gap(gap_size());
    this->revert_button()->set_visible(true);
    this->gizmo_callbacks().revert_requested = [this]() { m_callbacks.reset_clicked(); };

    this->add_smart_resolution_section(this->content());
    this->add_separator(this->content());
    this->add_blend_distance_section(this->content());
    this->add_separator(this->content());
    this->add_help_section(this->content());

    this->init_layer_height_profile_control();
}

VariableLayerHeightDialog::Callbacks& VariableLayerHeightDialog::callbacks()
{
    return m_callbacks;
}

void VariableLayerHeightDialog::add_smart_resolution_section(Item* item)
{
    Item* section_title_row = item->emplace_back<Item>();
    section_title_row->set_flex_shrink(0);
    Text* section_title     = section_title_row->emplace_back<Text>(_u8L("Smart resolution"));
    section_title->set_font_type(Render::ImguiFontType::Bold);

    Item* smart_resolution_slider_row = item->emplace_back<Item>();
    smart_resolution_slider_row->set_flex_shrink(0);
    smart_resolution_slider_row->set_gap(gap_size());

    Text* smart_resolution_slider_quality_label =
        smart_resolution_slider_row->emplace_back<Text>(_u8L("Quality"));
    smart_resolution_slider_quality_label->set_self_align(YGAlignCenter);

    m_smart_resolution_slider = Passthrough(std::make_unique<Slider>());
    m_smart_resolution_slider->set_begin_value(0.);
    m_smart_resolution_slider->set_end_value(1.);
    m_smart_resolution_slider->set_step(0.01);
    m_smart_resolution_slider->set_flex_grow(1);
    m_smart_resolution_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.smart_resolution_changed(value); };
    smart_resolution_slider_row->append(m_smart_resolution_slider.release());

    Text* smart_resolution_slider_speed_label =
        smart_resolution_slider_row->emplace_back<Text>(_u8L("Speed"));
    smart_resolution_slider_speed_label->set_self_align(YGAlignCenter);

    Item* auto_calculate_button_row = item->emplace_back<Item>();
    auto_calculate_button_row->set_flex_shrink(0);
    m_auto_calculate_button =
        auto_calculate_button_row->emplace_back<LayoutButton>(_u8L("Auto-calculate"));
    m_auto_calculate_button->label_object()->set_margin(BUTTON_TEXT_MARGIN);
    m_auto_calculate_button->callbacks().action = [this]()
    { m_callbacks.auto_calculate_clicked(); };
}

void VariableLayerHeightDialog::add_blend_distance_section(Item* item)
{
    Item* section_title_row = item->emplace_back<Item>();
    section_title_row->set_flex_shrink(0);
    Text* section_title     = section_title_row->emplace_back<Text>(_u8L("Blend Distance"));
    section_title->set_font_type(Render::ImguiFontType::Bold);

    Item* blend_distance_slider_row = item->emplace_back<Item>();
    blend_distance_slider_row->set_flex_shrink(0);
    blend_distance_slider_row->set_gap(gap_size());

    Text* blend_distance_slider_low_label =
        blend_distance_slider_row->emplace_back<Text>(_ctx_u8L("Low", "Blend Distance"));
    blend_distance_slider_low_label->set_self_align(YGAlignCenter);

    m_blend_distance_slider = Passthrough(std::make_unique<Slider>());
    m_blend_distance_slider->set_begin_value(1.);
    m_blend_distance_slider->set_end_value(10.);
    m_blend_distance_slider->set_step(1.);
    m_blend_distance_slider->set_flex_grow(1);
    m_blend_distance_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.blend_distance_changed(std::lround(value)); };
    blend_distance_slider_row->append(m_blend_distance_slider.release());

    Text* blend_distance_slider_high_label =
        blend_distance_slider_row->emplace_back<Text>(_ctx_u8L("High", "Blend Distance"));
    blend_distance_slider_high_label->set_self_align(YGAlignCenter);

    Item* lock_high_detail_toggle_row = item->emplace_back<Item>();
    lock_high_detail_toggle_row->set_flex_shrink(0);
    lock_high_detail_toggle_row->set_align_content(YGAlignFlexStart);
    lock_high_detail_toggle_row->set_gap(gap_size());

    m_lock_high_detail_toggle = lock_high_detail_toggle_row->emplace_back<ToggleButton>();
    m_lock_high_detail_toggle->callbacks().checked_changed = [this](bool checked)
    { m_callbacks.lock_high_detail_changed(checked); };

    Text* lock_high_detail_toggle_label =
        lock_high_detail_toggle_row->emplace_back<Text>(_u8L("Lock high detail"));
    lock_high_detail_toggle_label->set_self_align(YGAlignCenter);

    Item* smooth_button_row = item->emplace_back<Item>();
    smooth_button_row->set_flex_shrink(0);
    m_smooth_button         = smooth_button_row->emplace_back<LayoutButton>(_u8L("Smooth"));
    m_smooth_button->label_object()->set_margin(BUTTON_TEXT_MARGIN);
    m_smooth_button->callbacks().action = [this]() { m_callbacks.smooth_clicked(); };
}

void VariableLayerHeightDialog::add_help_section(Item* item)
{
    Item* help_section = item->emplace_back<Item>();
    help_section->set_orientation(Orientation::Vertical);
    help_section->set_min_height(50);
    help_section->set_align_items(YGAlignFlexStart);
    help_section->set_gap(gap_size());
    help_section->set_flex_shrink(0);

    GizmoHelpFactory help;
    help.init(help_section);
    help.add_item({GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}}, _u8L("Thinner layers"));
    help.add_item({GizmoHelpFactory::HelpIcon{Render::Icon::MouseRight}}, _u8L("Thicker layers"));
    help.add_item(
        {{"SHIFT"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Restore default height")
    );
    help.add_item(
        {{"SHIFT"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseRight}},
        _u8L("Manual blend")
    );
    help.add_item(
        {{"CTRL"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseWheel}},
        _u8L("Adjust brush size")
    );
    help.add_item(
        {{"ALT"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Edit height range")
    );
}

void VariableLayerHeightDialog::init_layer_height_profile_control()
{
    m_layer_height_profile_control =
        this->side_panel()->emplace_back<VariableLayerHeightControl>();

    m_layer_height_profile_control->callbacks().on_mouse_move =
        [this](std::optional<float> cursor_normalized_position)
    { m_callbacks.layer_profile_mouse_move(cursor_normalized_position); };

    m_layer_height_profile_control->callbacks().on_mouse_down =
        [this](
            float cursor_normalized_position,
            bool shift_down,
            bool ctrl_down,
            VariableLayerHeightControl::Button mouse_button
        )
    {
        m_callbacks.layer_profile_mouse_down(
            cursor_normalized_position,
            shift_down,
            ctrl_down,
            mouse_button
        );
    };

    m_layer_height_profile_control->callbacks().on_mouse_drag =
        [this](
            float cursor_normalized_position,
            bool shift_down,
            bool ctrl_down,
            VariableLayerHeightControl::Button mouse_button
        )
    {
        m_callbacks.layer_profile_mouse_drag(
            cursor_normalized_position,
            shift_down,
            ctrl_down,
            mouse_button
        );
    };

    m_layer_height_profile_control->callbacks().on_mouse_up =
        [this](VariableLayerHeightControl::Button mouse_button)
    { m_callbacks.layer_profile_mouse_up(mouse_button); };

    m_layer_height_profile_control->callbacks().on_mouse_wheel =
        [this](float mouse_wheel_delta, bool ctrl_down)
    { m_callbacks.layer_profile_mouse_wheel(mouse_wheel_delta, ctrl_down); };

    m_layer_height_profile_control->callbacks().on_height_range_click = [this]()
    { m_callbacks.on_height_range_click(); };
}

void VariableLayerHeightDialog::set_smart_resolution(const double smart_resolution)
{
    ASSERT(0. <= smart_resolution && smart_resolution <= 1.);
    m_smart_resolution_slider->set_value(smart_resolution);
}

void VariableLayerHeightDialog::set_blend_distance(const int blend_distance)
{
    ASSERT(1 <= blend_distance && blend_distance <= 10);
    m_blend_distance_slider->set_value(blend_distance);
}

void VariableLayerHeightDialog::set_lock_high_detail(const bool lock_high_detail)
{
    m_lock_high_detail_toggle->set_checked(lock_high_detail);
}

void VariableLayerHeightDialog::set_layer_height_title(const double layer_height)
{
    side_panel_header_title()->set_text(fmt::format("{:.3f}", layer_height));
}

void VariableLayerHeightDialog::set_layer_height_profile(const ZHeightPairs& layer_height_profile)
{
    m_layer_height_profile_control->set_layer_height_profile(layer_height_profile);
}

void VariableLayerHeightDialog::set_height_ranges(const HeightRangeEntries& height_ranges)
{
    m_layer_height_profile_control->set_height_ranges(height_ranges);
}

void VariableLayerHeightDialog::set_object_max_z(const float object_max_z)
{
    m_layer_height_profile_control->set_object_max_z(object_max_z);
}

void VariableLayerHeightDialog::set_min_layer_height(const float min_layer_height)
{
    m_layer_height_profile_control->set_min_layer_height(min_layer_height);
}

void VariableLayerHeightDialog::set_max_layer_height(const float max_layer_height)
{
    m_layer_height_profile_control->set_max_layer_height(max_layer_height);
}

void VariableLayerHeightDialog::set_default_layer_height(const float default_layer_height)
{
    m_layer_height_profile_control->set_default_layer_height(default_layer_height);
}

void VariableLayerHeightDialog::set_cursor_band_width(const float band_width)
{
    m_layer_height_profile_control->set_cursor_band_width(band_width);
}

void VariableLayerHeightDialog::set_cursor_normalized_position(const float normalized_position)
{
    ASSERT(0.f <= normalized_position && normalized_position <= 1.f);
    m_layer_height_profile_control->set_cursor_normalized_position(normalized_position);
}

void VariableLayerHeightDialog::reset_cursor_position()
{
    m_layer_height_profile_control->reset_cursor_position();
}

} // namespace Slic3r::App::Plater
