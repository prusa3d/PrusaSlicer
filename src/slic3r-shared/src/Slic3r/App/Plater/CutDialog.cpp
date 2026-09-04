
#include "Slic3r/App/Plater/CutDialog.hpp"

#include "Slic3r/App/Plater/GizmoWindow.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/InputTextWithSpin.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/App/Yoga/Circle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/Biz/Expr/Parser.hpp"
#include "Slic3r/Biz/Expr/Eval.hpp"
#include "Slic3r/Math.hpp"

#include <fmt/format.h>

using namespace Slic3r::Biz;
using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Yoga {

Slic3r::App::Yoga::PartProcessingItem::PartProcessingItem(
    const std::string& part_name,
    ImColor color
) :
    m_name(part_name)
{
    // set_flex_shrink(0);

    Item* group_item = this->emplace_back<Item>();
    group_item->set_gap(10.f);
    group_item->set_orientation(Orientation::Vertical);
    group_item->set_flex_grow(1.f);

    Item* header_item = group_item->emplace_back<Item>();
    header_item->set_gap(10.f);
    header_item->set_flex_grow(1.f);
    Circle* color_marker = header_item->emplace_back<Circle>();
    color_marker->set_fill(color);
    color_marker->set_height(16);

    m_label = header_item->emplace_back<Text>(m_name);
    m_label->set_font_type(Render::ImguiFontType::Bold);

    m_part_toggler = header_item->emplace_back<ToggleButton>();
    m_part_toggler->set_justify_content(YGJustifyFlexEnd);
    m_part_toggler->set_flex_grow(1);
    m_part_toggler->set_checked(true);
    m_part_toggler->callbacks().checked_changed = [this](bool checked)
    {
        m_state.keep = checked;
        set_enabled_buttons(checked);
        if (callbacks().checked_changed) {
            callbacks().checked_changed();
        }
    };

    m_keep_btn         = group_item->emplace_back<RadioButton>(_u8L("Keep orientation"));
    m_place_on_cut_btn = group_item->emplace_back<RadioButton>(_u8L("Place on cut"));
    m_flip_btn         = group_item->emplace_back<RadioButton>(_u8L("Flip upside down"));

    m_group.set_buttons({m_keep_btn, m_place_on_cut_btn, m_flip_btn});
    m_group.callbacks().action = [this](AbstractButton* btn)
    {
        m_state.keep         = is_checked() || btn == m_keep_btn;
        m_state.place_on_cut = btn == m_place_on_cut_btn;
        m_state.flip         = btn == m_flip_btn;
        if (callbacks().part_action_changed) {
            callbacks().part_action_changed();
        }
    };
}

PartProcessingItem::~PartProcessingItem() {}

PartProcessingItem::Callbacks& PartProcessingItem::callbacks()
{
    return m_callbacks;
}

void PartProcessingItem::set_as_part(bool is_part)
{
    const std::string new_name =
        fmt::format("{} {}", is_part ? _u8L("Part") : _u8L("Object"), m_name);
    if (m_label->text() == new_name)
        return;

    m_label->set_text(new_name);

    if (is_part) {
        m_part_toggler->set_checked(true);
        m_keep_btn->set_checked(true);
    }

    set_enabled(!is_part);
    set_enabled_buttons(!is_part);
}

void PartProcessingItem::set_enabled_buttons(bool enabled)
{
    for (AbstractButton* btn : m_group.buttons()) {
        btn->set_enabled(enabled);
    }
}

bool PartProcessingItem::is_checked() const
{
    return m_part_toggler->checked();
}

void PartProcessingItem::set_enabled_toggler(bool enabled)
{
    m_part_toggler->set_enabled(enabled);
}

const Undo::CutGizmoState::PartState& PartProcessingItem::state() const
{
    return m_state;
}

void PartProcessingItem::set_state(const Undo::CutGizmoState::PartState& state)
{
    m_state = state;

    // Note: state.keep is not mutually exclusive with state.flip or state.place_on_cut.
    // To ensure the correct button is selected, state.keep is evaluated last.
    AbstractButton* btn = state.flip ? m_flip_btn :
        state.place_on_cut           ? m_place_on_cut_btn :
                                       m_keep_btn;
    btn->set_checked(true);

    m_part_toggler->set_checked(state.keep);
}

} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

using namespace Slic3r::App::Yoga;

CutDialog::Callbacks& CutDialog::callbacks()
{
    return m_callbacks;
}

static constexpr ImColor build_volume_color{192, 154, 247};

CutDialog::CutDialog() : GizmoWindow()
{
    LayoutButton* reset_cut_btn = revert_button();
    reset_cut_btn->set_tooltip(_u8L("Reset cutting plane and remove connectors"));
    reset_cut_btn->set_visible(true);
    gizmo_callbacks().revert_requested = [this]()
    {
        if (callbacks().reset_cut_plane) {
            callbacks().reset_cut_plane();
        }
        if (callbacks().reset_connectors) {
            callbacks().reset_connectors();
        }
    };

    content()->set_padding({content()->padding().left, 20.f, content()->padding().right, 0.f});
    content()->set_gap(2.f * gap_size());

    init_connectors_header();

    init_connectors_input_panel();
    init_cut_plane_input_panel();

    // empty item - stretch spacer
    content()->emplace_back<Item>()->set_flex_grow(1);

    add_separator(content());
    init_action_buttons();
}

void CutDialog::init_action_buttons()
{
    bottom_bar()->set_flex_shrink(0);
    bottom_bar()->set_padding(2.f * gap_size());
    bottom_bar()->set_gap(gap_size());

    m_perform_btn = bottom_bar()->emplace_back<LayoutButton>(_u8L("Perform cut"));
    m_perform_btn->set_flex_grow(1.f);
    m_perform_btn->callbacks().action = [this]()
    {
        if (callbacks().perform) {
            callbacks().perform();
        }
    };

    m_confirm_connectors_btn = bottom_bar()->emplace_back<LayoutButton>(
        _u8L("Confirm"),
        Render::Icon::None,
        _u8L("Confirm connectors")
    );
    m_confirm_connectors_btn->set_flex_grow(1.f);
    m_confirm_connectors_btn->callbacks().action = [this]() { confirm_connectors(); };

    for (LayoutButton* btn :
         std::initializer_list<LayoutButton*>{m_perform_btn, m_confirm_connectors_btn})
    {
        btn->set_background_color(m_theme->color_imgui(Platform::Color::AccentPrimary));
        btn->set_label_font_type(Render::ImguiFontType::Bold);
        btn->set_content_padding(gap_size());
    }

    m_cancel_connectors_btn = bottom_bar()->emplace_back<LayoutButton>(_u8L("Cancel"));
    m_cancel_connectors_btn->set_background_color(m_theme->color_imgui(Platform::Color::Button));
    m_cancel_connectors_btn->set_content_padding(gap_size());
    m_cancel_connectors_btn->callbacks().action = [this]()
    {
        connectors_editing = false;
        if (callbacks().connectors_editing_changed) {
            callbacks().connectors_editing_changed(connectors_editing);
        }
        update_panels_visibility();
        if (callbacks().reset_connectors) {
            callbacks().reset_connectors();
        }
    };
}

void CutDialog::init_cut_plane_input_panel()
{
    m_cut_plane_input_panel = content()->emplace_back<Item>();
    m_cut_plane_input_panel->set_orientation(Orientation::Vertical);
    m_cut_plane_input_panel->set_align_content(YGAlign::YGAlignFlexStart);
    m_cut_plane_input_panel->set_flex_grow(1.f);
    m_cut_plane_input_panel->set_flex_shrink(0.f);
    m_cut_plane_input_panel->set_gap(content()->gap());

    Item* cut_plane_settings_panel =
        add_non_shrinked_wrap(m_cut_plane_input_panel, Orientation::Vertical, gap_size());

    m_mode_row          = add_row(_u8L("Mode"), cut_plane_settings_panel);
    m_planar_mode_btn   = add_icon_button(m_mode_row, Render::Icon::DividingLine, _u8L("Planar"));
    m_dovetail_mode_btn = add_icon_button(m_mode_row, Render::Icon::Dove, _u8L("Dovetail"));
    m_mode_group.set_buttons({m_planar_mode_btn, m_dovetail_mode_btn});
    m_mode_group.callbacks().action = [this](AbstractButton* btn)
    {
        set_planar_mode(btn == m_planar_mode_btn);
        if (callbacks().mode_changed) {
            callbacks().mode_changed();
        }
    };

    Item* cut_position_row = add_row(_u8L("Cut position"), cut_plane_settings_panel);
    cut_position_row->emplace_back<Text>("Z")->set_text_color(ImColor{64, 200, 232});
    m_cut_position = cut_position_row->emplace_back<InputTextField>();
    m_cut_position->set_width(50);
    std::unique_ptr<DoubleValidator> validator = std::make_unique<DoubleValidator>();
    validator->set_precision(2);
    m_cut_position->set_validator(std::move(validator));
    m_cut_position->callbacks().text_changed = [this]()
    {
        if (callbacks().z_changed) {
            Slic3r::Biz::Expr::Parser parser;
            Slic3r::Biz::Expr::Eval eval;
            try {
                double new_z = boost::get<double>(eval.eval(parser.parse(m_cut_position->text())));
                callbacks().z_changed(new_z);
            } catch ([[maybe_unused]] const Biz::Expr::ParseError& error) {
            } catch ([[maybe_unused]] const Biz::Expr::EvalError& error) {
            }
        }
    };

    LayoutButton* revert_cut_position_btn =
        add_revert_btn(cut_position_row, _u8L("Reset cutting plane"));
    m_cut_position->set_revert_button(revert_cut_position_btn);

    Item* cut_into_row = add_row(_u8L("Cut into") + ":", cut_plane_settings_panel);
    m_cut_into_row     = cut_into_row->parent_item();

    m_cut_into_combo = cut_into_row->emplace_back<ComboBox>("Cut into");
    m_cut_into_combo->set_flex_grow(1);
    // TRN CutGizmo: ComboBox "Cut into" ...
    m_cut_into_combo->set_items({_u8L("Objects"), _u8L("Parts")});
    m_cut_into_combo->callbacks().selection_changed = [this](int index)
    {
        keep_as_parts = index == 1;
        update_cut_into_states();
        if (callbacks().cut_settings_changed) {
            callbacks().cut_settings_changed(false);
        }
    };

    add_groove_input_panel();
    add_cut_settings();
    add_connectors_editing_buttons();

    add_separator(m_cut_plane_input_panel);

    Item* build_volume_panel =
        add_non_shrinked_wrap(m_cut_plane_input_panel, Orientation::Vertical, gap_size());

    build_volume_panel->emplace_back<Text>(_u8L("Build Volume"))
        ->set_font_type(Render::ImguiFontType::Bold);

    Item* build_volume = build_volume_panel->emplace_back<Item>();
    build_volume->set_justify_content(YGJustifySpaceBetween);

    for (Text** build_volume_axes :
         std::initializer_list<Text**>{
             &m_build_volume_x,
             &m_build_volume_y,
             &m_build_volume_z,
         })
    {
        (*build_volume_axes) = build_volume->emplace_back<Text>("* mm");
        (*build_volume_axes)->set_text_color(build_volume_color);
        (*build_volume_axes)->set_font_type(Render::ImguiFontType::Bold);
    }

    add_cut_plane_help_panel();

    // empty item - stretch spacer
    m_cut_plane_input_panel->emplace_back<Item>()->set_flex_grow(1);
}

void CutDialog::add_cut_plane_help_panel()
{
    add_separator(m_cut_plane_input_panel);

    Item* help_area = m_cut_plane_input_panel->emplace_back<Item>();
    help_area->set_justify_content(YGJustify::YGJustifyFlexStart);

    GizmoHelpFactory help;
    help.init(help_area);
    help.add_item({{"SHIFT"}}, _u8L("Hold to draw a cut line"));
}

void CutDialog::add_angles_row(
    Item* parent,
    InputTextWithSpin** first_input,
    const std::string& first_input_tooltip,
    const std::string& first_revert_tooltip,
    InputTextWithSpin** second_input,
    const std::string& second_input_tooltip,
    const std::string& second_revert_tooltip
)
{
    Item* angles_row = add_labeled_row(parent, _u8L("Angles"));

    auto add_item = [angles_row, this](
                        InputTextWithSpin** input,
                        const std::string& input_tooltip,
                        const std::string& revert_tooltip,
                        int min,
                        int max
                    )
    {
        Item* wrap = add_flex_shrinked_wrap(angles_row);

        (*input) = wrap->emplace_back<InputTextWithSpin>(std::make_unique<IntValidator>(min, max));
        (*input)->set_tooltip(input_tooltip);
        (*input)->set_flex_grow(1.f);

        Text* unit_text = wrap->emplace_back<Text>(std::string("°"));
        unit_text->set_self_align(YGAlignCenter);

        (*input)->set_revert_button(add_revert_btn(wrap, revert_tooltip));
    };

    add_item(first_input, first_input_tooltip, first_revert_tooltip, 30, 120);
    add_item(second_input, second_input_tooltip, second_revert_tooltip, 0, 15);
}

void CutDialog::add_tolerances_row(
    Item* parent,
    InputTextField** first_input,
    const std::string& first_input_tooltip,
    InputTextField** second_input,
    const std::string& second_input_tooltip
)
{
    Item* tolerances_row = add_labeled_row(parent, _u8L("Tolerances"));

    auto add_item =
        [this, tolerances_row](InputTextField** input, const std::string& input_tooltip) -> void
    {
        Item* wrap = add_flex_shrinked_wrap(tolerances_row);

        (*input) = wrap->emplace_back<InputTextField>();
        (*input)->set_tooltip(input_tooltip);
        (*input)->set_flex_grow(1.f);

        Text* unit_text = wrap->emplace_back<Text>(_u8L("mm"));
        unit_text->set_self_align(YGAlignCenter);

        // Means that unit will be changed in respect to the "use_inches" app_config option
        // so, add it to the m_units vector
        m_units.emplace_back(unit_text);
    };

    add_item(first_input, first_input_tooltip);
    add_item(second_input, second_input_tooltip);
}

void CutDialog::add_groove_input_panel()
{
    m_groove_input_panel = m_cut_plane_input_panel->emplace_back<Item>();
    m_groove_input_panel->set_orientation(Orientation::Vertical);
    m_groove_input_panel->set_align_content(YGAlign::YGAlignFlexStart);
    m_groove_input_panel->set_gap(gap_size());

    add_separator(m_groove_input_panel);

    add_row_with_slider(
        m_groove_input_panel,
        &m_groove_depth_value,
        _u8L("Depth"),
        _u8L("mm"),
        _u8L("Revert depth")
    );
    m_groove_depth_value->callbacks().value_changed = [this](double value)
    {
        if (callbacks().groove_depth_value_changed) {
            callbacks().groove_depth_value_changed(value);
        }
    };

    add_row_with_slider(
        m_groove_input_panel,
        &m_groove_width_value,
        _u8L("Width"),
        _u8L("mm"),
        _u8L("Revert width")
    );
    m_groove_width_value->callbacks().value_changed = [this](double value)
    {
        if (callbacks().groove_width_value_changed) {
            callbacks().groove_width_value_changed(value);
        }
    };

    add_tolerances_row(
        m_groove_input_panel,
        &m_groove_depth_tolerance,
        _u8L("Depth tolerance"),
        &m_groove_width_tolerance,
        _u8L("Width tolerance")
    );

    m_groove_depth_tolerance->callbacks().text_edited = [this]()
    {
        const double value = std::stod(m_groove_depth_tolerance->text());
        if (callbacks().groove_depth_tolerance_changed) {
            callbacks().groove_depth_tolerance_changed(value);
        }
    };
    m_groove_width_tolerance->callbacks().text_edited = [this]()
    {
        const double value = std::stod(m_groove_width_tolerance->text());
        if (callbacks().groove_width_tolerance_changed) {
            callbacks().groove_width_tolerance_changed(value);
        }
    };

    add_angles_row(
        m_groove_input_panel,
        &m_flap_angle,
        _u8L("Flap Angle"),
        _u8L("Revert flap angle"),
        &m_groove_angle,
        _u8L("Groove Angle"),
        _u8L("Revert groove angle")
    );

    m_flap_angle->callbacks().text_edited = [this]()
    {
        const double value =
            static_cast<double>(dynamic_cast<IntValidator*>(m_flap_angle->validator())->value());
        if (callbacks().flap_angle_changed) {
            callbacks().flap_angle_changed(value);
        }
    };

    m_groove_angle->callbacks().text_edited = [this]()
    {
        const double value =
            static_cast<double>(dynamic_cast<IntValidator*>(m_groove_angle->validator())->value());
        if (callbacks().groove_angle_changed) {
            callbacks().groove_angle_changed(value);
        }
    };

    m_groove_input_panel->set_visible(false);
}

void CutDialog::add_cut_settings()
{
    add_separator(m_cut_plane_input_panel);

    Item* parts_wrap_item = m_cut_plane_input_panel->emplace_back<Item>();
    parts_wrap_item->set_orientation(Orientation::Vertical);
    parts_wrap_item->set_gap(content()->gap());

    // create PartProcessingRow

    m_part_A = parts_wrap_item->emplace_back<PartProcessingItem>("A", ImColor{64, 191, 191});
    add_separator(parts_wrap_item);
    m_part_B = parts_wrap_item->emplace_back<PartProcessingItem>("B", ImColor{191, 64, 191});

    m_part_A->set_as_part(false);
    m_part_B->set_as_part(false);

    m_part_A->callbacks().checked_changed = [this]()
    {
        update_keep_object_warning();
        m_add_connectors_btn->set_enabled(!keep_as_parts && keep_upper() && keep_lower());
        if (callbacks().cut_settings_changed) {
            callbacks().cut_settings_changed(true);
        }
    };
    m_part_A->callbacks().part_action_changed = [this]()
    {
        update_keep_object_warning();
        if (callbacks().cut_settings_changed) {
            callbacks().cut_settings_changed(true);
        }
    };

    m_part_B->callbacks().checked_changed = [this]()
    {
        update_keep_object_warning();
        m_add_connectors_btn->set_enabled(!keep_as_parts && keep_upper() && keep_lower());
        if (callbacks().cut_settings_changed) {
            callbacks().cut_settings_changed(true);
        }
    };
    m_part_B->callbacks().part_action_changed = [this]()
    {
        update_keep_object_warning();
        if (callbacks().cut_settings_changed) {
            callbacks().cut_settings_changed(true);
        }
    };
}

void CutDialog::add_connectors_editing_buttons()
{
    m_connectors_editing_buttons = m_cut_plane_input_panel->emplace_back<Item>();
    m_connectors_editing_buttons->set_orientation(Orientation::Vertical);
    m_connectors_editing_buttons->set_gap(content()->gap());

    add_separator(m_connectors_editing_buttons);

    add_row_with_button(
        m_connectors_editing_buttons,
        &m_add_connectors_btn,
        _u8L("Add connectors")
    );
    m_add_connectors_btn->set_background_color(m_theme->color_imgui(Platform::Color::Button));
    m_add_connectors_btn->callbacks().action = [this]()
    {
        connectors_editing = true;
        if (callbacks().connectors_editing_changed) {
            callbacks().connectors_editing_changed(connectors_editing);
        }
        update_panels_visibility();
    };
}

void CutDialog::update_state(OutState state)
{
    bool connectors_warnig = state.connectors_outside_cut_contour > 0
        || state.connectors_outside_object > 0
        || state.connectors_overlap;

    bool has_warnings = connectors_warnig
        || state.plane_outside_object
        || state.invalid_groove
        || (!keep_upper() && !keep_lower());

    m_perform_btn->set_visible(!connectors_editing);
    m_perform_btn->set_enabled(!has_warnings);

    m_confirm_connectors_btn->set_visible(connectors_editing);
    m_cancel_connectors_btn->set_visible(connectors_editing);

    m_connectors_warnings.clear();

    if (connectors_warnig) {
        if (state.connectors_outside_cut_contour > size_t(0)) {
            m_connectors_warnings.emplace_back(
                fmt::vformat(
                    _L_PLURAL_u8(
                        "{} connector is out of cut contour",
                        "{} connectors are out of cut contour",
                        state.connectors_outside_cut_contour
                    ),
                    fmt::make_format_args(state.connectors_outside_cut_contour)
                )
            );
        }
        if (state.connectors_outside_object > size_t(0)) {
            m_connectors_warnings.emplace_back(
                fmt::vformat(
                    _L_PLURAL_u8(
                        "{} connector is out of object",
                        "{} connectors are out of object",
                        state.connectors_outside_object
                    ),
                    fmt::make_format_args(state.connectors_outside_object)
                )
            );
        }
        if (state.connectors_overlap) {
            m_connectors_warnings.emplace_back(_u8L("Some connectors are overlapped"));
        }
    }

    if (state.invalid_groove) {
        m_connectors_warnings.emplace_back(_u8L("Cut plane with groove is invalid"));
    }
    if (state.plane_outside_object) {
        m_connectors_warnings.emplace_back(_u8L("Cut plane is placed out of object"));
    }

    update_warnings();

    m_part_A->set_enabled_toggler(!state.has_connectors);
    m_part_B->set_enabled_toggler(!state.has_connectors);
    m_cut_into_row->set_enabled(!state.has_connectors);
    m_remove_connectors_btn->set_visible(state.has_connectors);
    m_mode_row->set_enabled(!state.has_connectors);
}

void CutDialog::update_from_gizmo_state(const Undo::CutGizmoState& state)
{
    set_planar_mode(state.is_planar_mode);
    if (state.is_planar_mode && state.connectors_editing) {
        force_connectors_editing();
    }

    keep_as_parts = state.keep_as_parts;
    m_cut_into_combo->set_current_index(keep_as_parts ? 1 : 0);
    update_cut_into_states();

    m_part_A->set_state(state.part_state_upper);
    m_part_B->set_state(state.part_state_lower);

    update_keep_object_warning();
}

const Undo::CutGizmoState::PartState& CutDialog::part_state_upper() const
{
    return m_part_A->state();
}

const Undo::CutGizmoState::PartState& CutDialog::part_state_lower() const
{
    return m_part_B->state();
}

bool CutDialog::keep_upper() const
{
    return m_part_A->state().keep;
}

bool CutDialog::keep_lower() const
{
    return m_part_B->state().keep;
}

bool CutDialog::place_on_cut_upper() const
{
    return m_part_A->state().place_on_cut;
}

bool CutDialog::place_on_cut_lower() const
{
    return m_part_B->state().place_on_cut;
}

bool CutDialog::flip_upper() const
{
    return m_part_A->state().flip;
}

bool CutDialog::flip_lower() const
{
    return m_part_B->state().flip;
}

void CutDialog::update_panels_visibility()
{
    m_connectors_input_panel->set_visible(connectors_editing);
    m_cut_plane_input_panel->set_visible(!connectors_editing);
    m_groove_input_panel->set_visible(!is_planar_cut_mode);

    top_bar()->set_visible(connectors_editing);
    m_confirm_connectors_btn->set_visible(connectors_editing);
    m_cancel_connectors_btn->set_visible(connectors_editing);
    m_perform_btn->set_visible(!connectors_editing);
}

void CutDialog::update_keep_object_warning()
{
    m_has_keep_object_warning = !keep_upper() && !keep_lower();
    m_perform_btn->set_enabled(!m_has_keep_object_warning);
    update_warnings();
}

void CutDialog::update_warnings()
{
    ASSERT(!m_has_keep_object_warning || m_connectors_warnings.empty());
    // "keep object" and "connectors" warnings are mutually exclusive,
    // so they are handled separately.
    if (m_has_keep_object_warning) {
        set_warning(
            _u8L("Cut tool issues"),
            _u8L("Select at least one object to keep after cutting.")
        );
    } else if (!m_connectors_warnings.empty()) {
        set_warning(_u8L("Cut tool issues"), m_connectors_warnings);
    } else {
        clear_warning();
    }
}

void CutDialog::update_cut_into_states()
{
    m_part_A->set_as_part(keep_as_parts);
    m_part_B->set_as_part(keep_as_parts);
    m_add_connectors_btn->set_enabled(!keep_as_parts && keep_upper() && keep_lower());
}

void CutDialog::confirm_connectors()
{
    connectors_editing = false;
    if (callbacks().connectors_editing_changed) {
        callbacks().connectors_editing_changed(connectors_editing);
    }
    update_panels_visibility();
}

void CutDialog::init_connectors_header()
{
    top_bar()->set_gap(10.);
    top_bar()->set_orientation(Orientation::Vertical);
    top_bar()->set_padding(5.f);

    Item* header = top_bar()->emplace_back<Item>();
    header->set_gap(gap_size());
    header->set_padding(5.f);
    header->set_align_items(YGAlignCenter);
    header->set_flex_shrink(0);
    LayoutButton* back_btn = header->emplace_back<LayoutButton>("", Render::Icon::ChevronLeft);
    back_btn->callbacks().action = [this]() { confirm_connectors(); };
    back_btn->set_height(24.f);
    Text* text = header->emplace_back<Text>(_u8L("Connectors"));
    text->set_self_align(YGAlignCenter);
    text->set_font_type(Render::ImguiFontType::Bold);

    // Proces reset button in taskbar
    m_remove_connectors_btn = add_revert_btn(header, _u8L("Remove connectors"));
    m_remove_connectors_btn->set_background_color(Platform::Color::ButtonTransparent);
    m_remove_connectors_btn->callbacks().action = [this]()
    {
        if (callbacks().reset_connectors) {
            callbacks().reset_connectors();
        }
    };

    add_separator(top_bar())->set_margin({ 0, -5.f, 0, 0 });
}

void CutDialog::init_connectors_input_panel()
{
    m_connectors_input_panel =
        add_non_shrinked_wrap(content(), Orientation::Vertical, content()->gap());

    Item* connector_attributes_panel =
        add_non_shrinked_wrap(m_connectors_input_panel, Orientation::Vertical, gap_size());

    m_type_row = add_row(_u8L("Type"), connector_attributes_panel);

    m_plug_btn  = add_icon_button(m_type_row, Render::Icon::PlugMarker, _u8L("Plug"));
    m_dowel_btn = add_icon_button(m_type_row, Render::Icon::DowelMarker, _u8L("Dowel"));
    m_snap_btn  = add_icon_button(m_type_row, Render::Icon::SnapMarker, _u8L("Snap"));

    m_connector_type_group.set_buttons({m_plug_btn, m_dowel_btn, m_snap_btn});
    m_connector_type_group.set_always_checked(false);
    m_connector_type_group.callbacks().action = [this](AbstractButton* btn)
    {
        const Domain::CutConnectorType type = btn == m_plug_btn ? Domain::CutConnectorType::Plug :
            btn == m_dowel_btn                                  ? Domain::CutConnectorType::Dowel :
                                                                  Domain::CutConnectorType::Snap;
        set_connector_type(type);

        if (callbacks().connector_attributes_changed) {
            callbacks().connector_attributes_changed(Biz::UndoSnapshotType::CutChangeConnectorType);
        }
    };

    m_style_row   = add_row(_u8L("Style"), connector_attributes_panel);
    m_prism_btn   = add_icon_button(m_style_row, Render::Icon::Prism, _u8L("Prism"));
    m_frustum_btn = add_icon_button(m_style_row, Render::Icon::Frustum, _u8L("Frustum"));
    m_connector_style_group.set_buttons({m_prism_btn, m_frustum_btn});
    m_connector_style_group.set_always_checked(false);
    m_connector_style_group.callbacks().action = [this](AbstractButton* btn)
    {
        set_connector_style(
            btn == m_prism_btn ? Domain::CutConnectorStyle::Prism :
                                 Domain::CutConnectorStyle::Frustum
        );
        if (callbacks().connector_attributes_changed) {
            callbacks().connector_attributes_changed(
                Biz::UndoSnapshotType::CutChangeConnectorStyle
            );
        }
    };

    m_shape_row    = add_row(_u8L("Shape"), connector_attributes_panel);
    m_triangle_btn = add_icon_button(m_shape_row, Render::Icon::Triangle, _u8L("Triangle"));
    m_square_btn   = add_icon_button(m_shape_row, Render::Icon::Square, _u8L("Square"));
    m_hexagon_btn  = add_icon_button(m_shape_row, Render::Icon::Hexagon, _u8L("Hexagon"));
    m_circle_btn   = add_icon_button(m_shape_row, Render::Icon::Circle, _u8L("Circle"));
    m_connector_shape_group.set_buttons(
        {m_triangle_btn, m_square_btn, m_hexagon_btn, m_circle_btn}
    );
    m_connector_shape_group.set_always_checked(false);
    m_connector_shape_group.callbacks().action = [this](AbstractButton* btn)
    {
        set_connector_shape(
            btn == m_triangle_btn   ? Domain::CutConnectorShape::Triangle :
                btn == m_square_btn ? Domain::CutConnectorShape::Square :
                btn == m_circle_btn ? Domain::CutConnectorShape::Circle :
                                      Domain::CutConnectorShape::Hexagon
        );
        if (callbacks().connector_attributes_changed) {
            callbacks().connector_attributes_changed(
                Biz::UndoSnapshotType::CutChangeConnectorShape
            );
        }
    };

    add_separator(m_connectors_input_panel);

    Item* connector_trafos_panel =
        add_non_shrinked_wrap(m_connectors_input_panel, Orientation::Vertical, gap_size());

    add_row_with_slider(
        connector_trafos_panel,
        &m_connector_depth_value,
        _u8L("Depth"),
        _u8L("mm"),
        _u8L("Revert depth")
    );
    m_connector_depth_value->callbacks().value_changed = [this](double value)
    {
        connector_depth = value;
        if (callbacks().connector_transformations_changed)
            callbacks().connector_transformations_changed();
    };

    add_row_with_slider(
        connector_trafos_panel,
        &m_connector_size_value,
        _u8L("Size"),
        _u8L("mm"),
        _u8L("Revert size")
    );
    m_connector_size_value->callbacks().value_changed = [this](double value)
    {
        connector_size = value;
        if (callbacks().connector_transformations_changed)
            callbacks().connector_transformations_changed();
    };

    add_tolerances_row(
        connector_trafos_panel,
        &m_connector_depth_tolerance,
        _u8L("Depth tolerance"),
        &m_connector_size_tolerance,
        _u8L("Size tolerance")
    );

    m_connector_depth_tolerance->callbacks().text_edited = [this]()
    {
        connector_depth_tolerance = std::stod(m_connector_depth_tolerance->text());
        if (callbacks().connector_transformations_changed) {
            callbacks().connector_transformations_changed();
        }
    };
    m_connector_size_tolerance->callbacks().text_edited = [this]()
    {
        connector_size_tolerance = std::stod(m_connector_size_tolerance->text());
        if (callbacks().connector_transformations_changed) {
            callbacks().connector_transformations_changed();
        }
    };

    add_row_with_spin_int(
        _u8L("Rotation"),
        connector_trafos_panel,
        &m_connector_rotation,
        std::string("°"),
        _u8L("Revert connector Z rotation"),
        0,
        180
    );
    m_connector_rotation->callbacks().text_edited = [this]()
    {
        connector_angle = static_cast<double>(
            dynamic_cast<IntValidator*>(m_connector_rotation->validator())->value()
        );
        if (callbacks().connector_transformations_changed)
            callbacks().connector_transformations_changed();
    };

    m_snap_bulge_row = add_row_with_spin_int(
        _u8L("Bulge"),
        connector_trafos_panel,
        &m_snap_bulge_proportion,
        std::string("%"),
        _u8L("Revert bulge proportion related to radius"),
        5,
        15
    );
    m_snap_bulge_proportion->callbacks().text_edited = [this]()
    {
        snap_bulge_proportion =
            dynamic_cast<IntValidator*>(m_snap_bulge_proportion->validator())->value();
        if (callbacks().snap_settings_changed)
            callbacks().snap_settings_changed();
    };

    m_snap_space_row = add_row_with_spin_int(
        _u8L("Space"),
        connector_trafos_panel,
        &m_snap_space_proportion,
        std::string("%"),
        _u8L("Revert space proportion related to radius"),
        10,
        50
    );
    m_snap_space_proportion->callbacks().text_edited = [this]()
    {
        snap_space_proportion =
            dynamic_cast<IntValidator*>(m_snap_space_proportion->validator())->value();

        // update m_snap_bulge_proportion related from its value
        IntValidator* int_validator =
            dynamic_cast<IntValidator*>(m_snap_bulge_proportion->validator());
        int_validator->set_to(snap_space_proportion);
        m_snap_bulge_proportion->set_text(int_validator->process(m_snap_bulge_proportion->text()));

        if (callbacks().snap_settings_changed)
            callbacks().snap_settings_changed();
    };

    add_separator(m_connectors_input_panel);

    LayoutButton* flip_cut_plane_btn{nullptr};
    add_row_with_button(m_connectors_input_panel, &flip_cut_plane_btn, _u8L("Flip cut plane"));
    flip_cut_plane_btn->callbacks().action = [this]()
    {
        if (callbacks().flip_cut_plane) {
            callbacks().flip_cut_plane();
        }
    };
    add_separator(m_connectors_input_panel);

    add_connectors_help_panel();

    m_connectors_input_panel->set_visible(false);
}

void CutDialog::add_connectors_help_panel()
{
    Item* help_area = m_connectors_input_panel->emplace_back<Item>();
    help_area->set_orientation(Orientation::Vertical);
    help_area->set_flex_grow(1.f);
    help_area->set_align_items(YGAlignFlexStart);
    help_area->set_gap(gap_size());

    const Unit mouse_help_size{20.f};

    GizmoHelpFactory help;
    help.init(help_area);
    help.add_item(
        {GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft, mouse_help_size, mouse_help_size}},
        _u8L("Add connector")
    );
    help.add_item(
        {GizmoHelpFactory::HelpIcon{Render::Icon::MouseRight, mouse_help_size, mouse_help_size}},
        _u8L("Remove connector")
    );
    help.add_item(
        {{"SHIFT"},
         GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft, mouse_help_size, mouse_help_size}},
        _u8L("Select multiple")
    );
    help.add_item(
        {{"ALT"},
         GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft, mouse_help_size, mouse_help_size}},
        _u8L("Remove from selection")
    );
    help.add_item({{"CTRL"}, {"A"}}, _u8L("Select all"));
}

Item* CutDialog::add_row(const std::string& title, Yoga::Item* parent)
{
    Item* row = parent->emplace_back<Item>();
    row->set_gap(3);

    Text* text = row->emplace_back<Text>(title);
    text->set_width(m_label_width);
    text->set_self_align(YGAlignCenter);

    Item* box = row->emplace_back<Item>();
    box->set_width_percent(70);
    box->set_gap(gap_size());
    box->set_align_items(YGAlignCenter);

    return box;
}

void CutDialog::set_build_size(Domain::Vec3d size, double default_z)
{
    static const double in_to_mm = 25.4;
    static const double mm_to_in = 1 / in_to_mm;

    std::string unit_str = m_imperial_units ? _u8L("in") : _u8L("mm");

    if (m_imperial_units) {
        size.x() *= mm_to_in;
        size.y() *= mm_to_in;
        size.z() *= mm_to_in;
    }

    m_build_volume_x->set_text(fmt::format("{0:.5g} {1}", size.x(), unit_str));
    m_build_volume_y->set_text(fmt::format("{0:.5g} {1}", size.y(), unit_str));
    m_build_volume_z->set_text(fmt::format("{0:.5g} {1}", size.z(), unit_str));
    m_cut_position->set_default(default_z);
}

void CutDialog::set_cut_z_position(double cut_z_position)
{
    m_cut_position->set_text(fmt::format("{0:.2f}", cut_z_position));
}

void CutDialog::set_planar_mode(bool is_planar)
{
    is_planar_cut_mode = is_planar;
    if (is_planar_cut_mode) {
        m_planar_mode_btn->set_checked(true);
    } else {
        m_dovetail_mode_btn->set_checked(true);
    }

    // disable connectors panel
    connectors_editing = false;

    update_panels_visibility();

    if (!is_planar_cut_mode) {
        m_cut_into_combo->set_current_index(0);
        keep_as_parts = false;
        if (callbacks().cut_settings_changed) {
            callbacks().cut_settings_changed(false);
        }
        update_cut_into_states();
    }

    // disable buttons for dovetail mode
    m_cut_into_combo->set_enabled(is_planar_cut_mode);
    m_cut_into_row->set_visible(is_planar_cut_mode);
    m_connectors_editing_buttons->set_visible(is_planar_cut_mode);
}

void CutDialog::force_connectors_editing()
{
    m_add_connectors_btn->callbacks().action();
}

void CutDialog::set_connector_type(Domain::CutConnectorType type)
{
    if (type == Domain::CutConnectorType::Undef) {
        m_dowel_btn->set_checked(false);
        m_plug_btn->set_checked(false);
        m_snap_btn->set_checked(false);
        return;
    }
    connector_type = type;
    switch (type) {
    case Domain::CutConnectorType::Dowel:
        m_dowel_btn->set_checked(true);
        break;
    case Domain::CutConnectorType::Plug:
        m_plug_btn->set_checked(true);
        break;
    case Domain::CutConnectorType::Snap:
        m_snap_btn->set_checked(true);
        break;
    default:
        break;
    }

    const bool is_snap = type == Domain::CutConnectorType::Snap;
    m_style_row->set_enabled(!is_snap);
    m_shape_row->set_enabled(!is_snap);
    m_snap_bulge_row->set_visible(is_snap);
    m_snap_space_row->set_visible(is_snap);

    if (type == Domain::CutConnectorType::Dowel) {
        m_prism_btn->set_checked(true);
        connector_style = Domain::CutConnectorStyle::Prism;
    }
    m_frustum_btn->set_enabled(type != Domain::CutConnectorType::Dowel);
}

void CutDialog::set_connector_style(Domain::CutConnectorStyle style)
{
    if (style == Domain::CutConnectorStyle::Undef) {
        m_frustum_btn->set_checked(false);
        m_prism_btn->set_checked(false);
        return;
    }
    connector_style = style;
    switch (style) {
    case Domain::CutConnectorStyle::Frustum:
        m_frustum_btn->set_checked(true);
        break;
    case Domain::CutConnectorStyle::Prism:
        m_prism_btn->set_checked(true);
        break;
    default:
        break;
    }
}

void CutDialog::set_connector_shape(Domain::CutConnectorShape shape)
{
    if (shape == Domain::CutConnectorShape::Undef) {
        m_triangle_btn->set_checked(false);
        m_square_btn->set_checked(false);
        m_hexagon_btn->set_checked(false);
        m_circle_btn->set_checked(false);
        return;
    }
    connector_shape = shape;
    switch (shape) {
    case Domain::CutConnectorShape::Triangle:
        m_triangle_btn->set_checked(true);
        break;
    case Domain::CutConnectorShape::Square:
        m_square_btn->set_checked(true);
        break;
    case Domain::CutConnectorShape::Hexagon:
        m_hexagon_btn->set_checked(true);
        break;
    case Domain::CutConnectorShape::Circle:
        m_circle_btn->set_checked(true);
        break;
    default:
        break;
    }
}

// Round doubles for 2 digits to correct behavior of revert buttons
static double round_2(double value)
{
    return std::round(value * 100.0) / 100.0;
};

static void set_slider(
    SliderWithInput* slider,
    double min_val,
    double max_val,
    double step,
    double value,
    std::optional<double>(default_value) = std::nullopt
)
{
    slider->set_begin_value(min_val);
    slider->set_end_value(max_val);
    slider->set_value(round_2(value));
    slider->set_step(step);
    if (default_value) {
        slider->set_default(round_2(default_value.value()));
    }
}

static void set_text_input(
    InputTextField* input,
    double min_val,
    double max_val,
    double value,
    std::optional<double>(default_value) = std::nullopt
)
{
    if (!input->validator()) {
        std::unique_ptr<DoubleValidator> validator = std::make_unique<DoubleValidator>();
        validator->set_precision(2);
        input->set_validator(std::move(validator));
    }

    DoubleValidator* double_validator = dynamic_cast<DoubleValidator*>(input->validator());
    double_validator->set_from(min_val);
    double_validator->set_to(max_val);
    input->set_text(
        fmt::format("{1:.{0}f}", double_validator->precision().value(), round_2(value))
    );
    if (default_value) {
        input->set_default(round_2(default_value.value()));
    }
}

static void set_spin_input(
    InputTextWithSpin* input,
    double value,
    double default_value,
    std::optional<double>(max_val) = std::nullopt
)
{
    if (max_val) {
        IntValidator* int_validator = dynamic_cast<IntValidator*>(input->validator());
        int_validator->set_to(max_val.value());
    }
    input->set_text(fmt::format("{:.10g}", value));
    input->set_default(round_2(default_value));
}

void CutDialog::set_groove_values(const Biz::Cut::Groove& m_groove, double max_elements_size)
{
    set_slider(
        m_groove_depth_value,
        1.,
        max_elements_size,
        0.01,
        m_groove.depth,
        m_groove.depth_init
    );
    double max_depth_tolerance = std::min(0.3 * m_groove.depth, 1.5);
    set_text_input(m_groove_depth_tolerance, 0., max_depth_tolerance, m_groove.depth_tolerance);

    set_slider(
        m_groove_width_value,
        1.,
        max_elements_size,
        0.01,
        m_groove.width,
        m_groove.width_init
    );
    double max_width_tolerance = std::min(0.3 * m_groove.width, 1.5);
    set_text_input(m_groove_width_tolerance, 0., max_width_tolerance, m_groove.width_tolerance);

    set_spin_input(m_flap_angle, rad2deg(m_groove.flaps_angle), rad2deg(m_groove.flaps_angle_init));
    set_spin_input(m_groove_angle, rad2deg(m_groove.angle), rad2deg(m_groove.angle_init));
}

void CutDialog::set_connector_defaults(double max_size)
{
    const double depth_min_value =
        connector_type == Domain::CutConnectorType::Snap ? connector_size : 1.;
    const double max_tolerance = 0.5 * max_size;

    set_slider(m_connector_depth_value, depth_min_value, max_size, 0.1, connector_depth, 3.);
    set_text_input(m_connector_depth_tolerance, 0., max_tolerance, connector_depth_tolerance, 0.1);
    set_slider(m_connector_size_value, 1., max_size, 0.1, connector_size, 2.5);
    set_text_input(m_connector_size_tolerance, 0., max_tolerance, connector_size_tolerance, 0.);
    set_spin_input(m_connector_rotation, connector_angle, 0.);

    set_spin_input(m_snap_bulge_proportion, snap_bulge_proportion, 15., snap_space_proportion);
    set_spin_input(m_snap_space_proportion, snap_space_proportion, 30.);

    // set attributes defaults
    set_connector_shape(Domain::CutConnectorShape::Circle);
    set_connector_style(Domain::CutConnectorStyle::Prism);
    set_connector_type(Domain::CutConnectorType::Plug);
}

void CutDialog::set_connector_values(
    std::optional<double> depth,
    std::optional<double> depth_tolerance,
    std::optional<double> half_size,
    std::optional<double> half_size_tolerance,
    std::optional<double> angle
)
{
    if (depth) {
        m_connector_depth_value->set_value(depth.value());
    } else {
        m_connector_depth_value->set_undef_value();
    }

    if (depth_tolerance) {
        m_connector_depth_tolerance->set_text(fmt::format("{1:.{0}f}", 2, depth_tolerance.value()));
    } else {
        m_connector_depth_tolerance->set_text("");
    }

    if (half_size) {
        m_connector_size_value->set_value(2. * half_size.value());
    } else {
        m_connector_size_value->set_undef_value();
    }

    if (half_size_tolerance) {
        m_connector_size_tolerance->set_text(
            fmt::format("{1:.{0}f}", 2, 2. * half_size_tolerance.value())
        );
    } else {
        m_connector_size_tolerance->set_text("");
    }

    if (angle) {
        m_connector_rotation->set_text(fmt::format("{:.10g}", rad2deg(angle.value())));
    } else {
        m_connector_rotation->set_text("");
    }
}

} // namespace Slic3r::App::Plater
