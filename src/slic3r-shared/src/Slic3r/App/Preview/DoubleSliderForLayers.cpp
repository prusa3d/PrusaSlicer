#include "Slic3r/App/Preview/DoubleSliderForLayers.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/Assert.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"

#include "Slic3r/App/CommandBindingManager.hpp"
#include "Slic3r/App/MenuItem.hpp"
#include "Slic3r/App/MenuManager.hpp"
#include "Slic3r/App/MenuBuilder.hpp"

#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/GCodeReader/Utils.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/Biz/Units.hpp"
#include <Slic3r/Biz/libpgcode/Utils.hpp>
#include <Slic3r/App/libvgcode/Types.hpp>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>

#include <imgui/imgui_stdlib.h>

#include <algorithm>

#include "Slic3r/LegacyFormat.hpp"
#include "libslic3r/CustomGCode.hpp"

using namespace Slic3r::Biz::libpgcode;
using namespace Slic3r::Biz;
using namespace Slic3r::App::libvgcode;

using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::Vec2f;

using Slic3r::Biz::Algorithms::Color::decode_color;
using Slic3r::Biz::Algorithms::Color::encode_color;
using Slic3r::Biz::GCodeReader::contains_reserved_tags;

namespace Slic3r::App::Preview {

namespace CustomGCode      = Domain::CustomGCode;
using FuncCommandExtraOpts = Platform::FuncCommandExtraOpts;

static constexpr float EPSILON = 0.0011f;

DoubleSliderForLayers::DoubleSliderForLayers() :
    Slic3r::App::Imgui::DoubleSlider::Manager<float>(
        std::string("LayersSlider"),
        L("Layers"),
        Yoga::Orientation::Vertical
    )
{
    set_flex_shrink(0);
    Yoga::Item* btns = emplace_back<Yoga::Item>();
    btns->set_gap(5);
    btns->set_justify_content(YGJustifyCenter);

    const ImColor color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    m_revert_btn        = btns->emplace_back<Yoga::LayoutButton>(
        std::string{},
        Render::Icon::DSRevert,
        _u8L("Discard all custom changes")
    );
    m_revert_btn->set_icon_tint(color);
    m_revert_btn->set_visible(can_edit());
    m_revert_btn->set_enabled(!m_ticks.empty());
    m_revert_btn->callbacks().action = [this]() { discard_all_ticks(); };
    m_revert_btn->set_background_color(Platform::Color::ButtonTransparent);

    m_lock_btn =
        btns->emplace_back<Yoga::LayoutButton>("", Render::Icon::Unlock, _u8L("One layer mode"));
    m_lock_btn->set_icon_tint(color);
    m_lock_btn->set_background_color(Platform::Color::ButtonTransparent);
    m_lock_btn->set_checkable(true);
    m_lock_btn->callbacks().action = [this]()
    {
        m_lock_btn->set_icon(m_lock_btn->checked() ? Render::Icon::Lock : Render::Icon::Unlock);
        change_one_layer_lock();
    };

    m_cog_btn = btns->emplace_back<Yoga::LayoutButton>("", Render::Icon::DSSettings);
    m_cog_btn->set_icon_tint(color);
    m_cog_btn->set_background_color(Platform::Color::ButtonTransparent);
    m_cog_btn->callbacks().action = [this]() { m_cog_menu->open(); };

    Vec2f btns_size = {22.f, 22.f};
    for (Yoga::LayoutButton* btn :
         std::initializer_list<Yoga::LayoutButton*>{m_revert_btn, m_lock_btn, m_cog_btn})
    {
        btn->set_min_width(btns_size.x());
        btn->set_min_height(btns_size.y());
        btn->set_max_width(btns_size.x());
        btn->set_max_height(btns_size.y());
    }

    m_ctrl->show_label_on_mouse_move(true);

    m_ctrl->set_get_label_on_move_cb(
        [this](int pos)
        {
            m_pos_on_move = pos;
            return m_show_estimated_times ? label(pos, LabelType::EstimatedTime) : "";
        }
    );
    m_ctrl->set_extra_draw_cb([this](const ImRect& draw_rc) { return draw_ticks(draw_rc); });

    m_ctrl->callbacks().value_changed       = [this]() { process_thumb_move(); };
    m_ctrl->callbacks().request_extra_frame = [this]() { process_request_extra_frames(); };
    m_ctrl->callbacks().extra_render        = [this]() { extra_render(); };

    m_ticks.set_values(&m_values);
}

void DoubleSliderForLayers::create_cog_menu(Item* parent)
{
    m_cog_menu = parent->emplace_back<Yoga::Menu>("cog_menu", Yoga::Position::Top);

    MenuBuilder menu_builder(*m_menu_manager, *m_command_binding_manager);
    menu_builder.add_menu_item(m_cog_menu, MenuItemName::JumpToValue);

    m_show_estimated_times_item =
        m_cog_menu->append_item(_u8L("Show estimated print time on hover"));
    m_show_estimated_times_item->set_checkable(true);
    m_show_estimated_times_item->set_checked(m_show_estimated_times);

    m_show_estimated_times_item->callbacks().checked_changed = [this](bool checked)
    {
        m_show_estimated_times = checked;
        if (m_cb_app_config_changed)
            m_cb_app_config_changed("show_estimated_times_in_dbl_slider", m_show_estimated_times);
    };

    Yoga::MenuItem* ruler_submenu = m_cog_menu->append_item(_u8L("Ruler"));

    m_show_ruler_item = ruler_submenu->append_sub_menu_item(_u8L("Show"));
    m_show_ruler_item->set_checkable(true);
    m_show_ruler_item->set_checked(m_show_ruler);
    m_show_ruler_item->callbacks().checked_changed = [this](bool checked)
    {
        toggle_show_ruler(checked);
        if (m_cb_app_config_changed) {
            m_cb_app_config_changed("show_ruler_in_dbl_slider", m_show_ruler);
        }
    };

    m_show_ruler_background_item = ruler_submenu->append_sub_menu_item(_u8L("Show background"));
    m_show_ruler_background_item->set_checkable(true);
    m_show_ruler_background_item->set_checked(m_show_ruler_bg);
    m_show_ruler_background_item->callbacks().checked_changed = [this](bool checked)
    {
        m_show_ruler_bg = checked;
        update_thumbs_border_color();

        // ToDo set transparent window BG, if m_show_ruler_bg == false
        if (m_cb_app_config_changed) {
            m_cb_app_config_changed("show_ruler_bg_in_dbl_slider", m_show_ruler_bg);
        }
    };

    m_cog_menu->append_separator();

    m_edit_extruder_sequence_menu_item =
        m_cog_menu->append_item(_u8L("Set extruder sequence for the entire print"));
    m_edit_extruder_sequence_menu_item->callbacks().action = [this]()
    {
        if (m_ticks.edit_extruder_sequence(m_ctrl->max_pos(), m_mode))
            process_ticks_changed();
    };

    m_seq_top_layer_only_item =
        m_cog_menu->append_item(_u8L("Sequential slider applied only to top layer"));
    m_seq_top_layer_only_item->set_checkable(true);
    m_seq_top_layer_only_item->set_checked(m_seq_top_layer_only);
    m_seq_top_layer_only_item->callbacks().action = [this]()
    {
        m_seq_top_layer_only = m_seq_top_layer_only_item->checked();
        if (m_cb_app_config_changed) {
            m_cb_app_config_changed("seq_top_layer_only", m_seq_top_layer_only);
        }
    };

    m_cog_menu->append_separator();

    bool use_default_colors        = m_ticks.use_default_colors();
    m_use_default_colors_menu_item = m_cog_menu->append_item(_u8L("Use default colors").c_str());
    m_use_default_colors_menu_item->set_checkable(true);
    m_use_default_colors_menu_item->set_checked(use_default_colors);
    m_use_default_colors_menu_item->callbacks().checked_changed = [this](bool checked)
    {
        set_use_default_colors(checked);
        if (m_cb_app_config_changed) {
            m_cb_app_config_changed("use_default_colors_in_dbl_slider", checked);
        }
    };

    // Auto color change functionality isn't implemented for now
    return;
    m_auto_color_change_menu_item = m_cog_menu->append_item(_u8L("Set auto color changes"));
    m_auto_color_change_menu_item->callbacks().action = [this]() { auto_color_change(); };
}

void DoubleSliderForLayers::update_visibility_cog_menu_items()
{
    if (m_cog_menu) {
        m_edit_extruder_sequence_menu_item->set_visible(
            false // m_mode == CustomGCode::Mode::MultiAsSingle && m_draw_mode == DrawMode::Regular// ysFIXME-spe3494
        );
        m_seq_top_layer_only_item->set_visible(m_draw_mode != DrawMode::SlaPrint);
        m_use_default_colors_menu_item->set_visible(
            can_edit()
            && m_mode == CustomGCode::Mode::SingleExtruder // ysFIXME-spe3494
            && m_draw_mode == DrawMode::Regular // ysFIXME-spe3494
        );
        if (m_auto_color_change_menu_item) {
            m_auto_color_change_menu_item->set_visible(
                can_edit()
                && m_mode != CustomGCode::Mode::MultiExtruder
                && m_draw_mode == DrawMode::Regular
            );
        }
    }
}

void DoubleSliderForLayers::change_one_layer_lock()
{
    m_ctrl->combine_thumbs(!m_ctrl->is_combine_thumbs());
    process_thumb_move();
}

CustomGCode::Info DoubleSliderForLayers::ticks_values() const
{
    CustomGCode::Info custom_gcode_per_print_z;
    std::vector<CustomGCode::Item>& values = custom_gcode_per_print_z.gcodes;

    int val_size = int(m_values.size());
    if (!m_values.empty()) {
        for (const TickCode& tick : m_ticks.ticks) {
            if (tick.tick > val_size)
                break;
            values.emplace_back(
                CustomGCode::Item{
                    m_values[tick.tick],
                    tick.type,
                    tick.extruder,
                    tick.color,
                    tick.extra
                }
            );
        }
    }
    custom_gcode_per_print_z.mode = m_mode;
    return custom_gcode_per_print_z;
}

void DoubleSliderForLayers::set_ticks_values(const CustomGCode::Info& custom_gcode_per_print_z)
{
    if (m_values.empty()) {
        m_ticks.mode = m_mode;
        return;
    }

    m_ticks.set_ticks(custom_gcode_per_print_z);
    m_revert_btn->set_enabled(!m_ticks.empty());

    update_draw_scroll_line_cb();
}

void
DoubleSliderForLayers::set_layers_times(const std::vector<float>& layers_times, float total_time)
{
    m_layers_times.clear();
    if (layers_times.empty())
        return;
    m_layers_times.resize(layers_times.size(), 0.0);
    m_layers_times[0] = layers_times[0];
    for (size_t i = 1; i < layers_times.size(); i++)
        m_layers_times[i] = m_layers_times[i - 1] + layers_times[i];

    // Erase duplicates values from m_values and save it to the m_layers_values
    // They will be used for show the correct estimated time for MM print, when "No sparce layer" is enabled
    // See https://github.com/prusa3d/PrusaSlicer/issues/6232
    if (m_ticks.is_wipe_tower && m_values.size() != m_layers_times.size()) {
        m_layers_values = m_values;
        std::sort(m_layers_values.begin(), m_layers_values.end());
        m_layers_values.erase(
            std::unique(m_layers_values.begin(), m_layers_values.end()),
            m_layers_values.end()
        );

        // When whipe tower is used to the end of print, there is one layer which is not marked in layers_times
        // So, add this value from the total print time value
        if (m_layers_values.size() != m_layers_times.size())
            for (size_t i = m_layers_times.size(); i < m_layers_values.size(); i++)
                m_layers_times.push_back(total_time);
    }
}

void DoubleSliderForLayers::set_layers_times(const std::vector<float>& layers_times)
{
    m_ticks.is_wipe_tower = false;
    m_layers_times        = layers_times;
    std::copy(layers_times.begin(), layers_times.end(), m_layers_times.begin());
}

void DoubleSliderForLayers::set_draw_mode(bool is_sla_print, bool is_sequential_print)
{
    m_draw_mode = is_sla_print ? DrawMode::SlaPrint :
        is_sequential_print    ? DrawMode::SequentialFffPrint :
                                 DrawMode::Regular;

    update_draw_scroll_line_cb();

    update_visibility_cog_menu_items();
    m_revert_btn->set_visible(can_edit());
}

void DoubleSliderForLayers::set_mode_and_only_extruder(
    const bool is_one_extruder_printed_model,
    const int only_extruder
)
{
    m_mode = !is_one_extruder_printed_model ? CustomGCode::Mode::MultiExtruder :
        only_extruder < 0                   ? CustomGCode::Mode::SingleExtruder :
                                              CustomGCode::Mode::MultiAsSingle;
    if ((m_ticks.mode == CustomGCode::Mode::Undef) || (m_ticks.empty() && m_ticks.mode != m_mode))
        m_ticks.mode = m_mode;

    m_ticks.only_extruder_id = only_extruder;
    m_ticks.is_wipe_tower    = m_mode != CustomGCode::Mode::SingleExtruder;

    if (m_mode != CustomGCode::Mode::SingleExtruder) {
        set_use_default_colors(false);
        // ysFIXME-spe3494
        if (m_ticks.erase_all_ticks_with_code(Domain::CustomGCode::Type::ColorChange)) {
            process_ticks_changed();
        }
    }

    update_visibility_cog_menu_items();
    m_cog_btn->set_tooltip(
        // ysFIXME-spe3494
        // m_mode == CustomGCode::Mode::MultiAsSingle ?
        // format(
        // _u8L(
        // "Jump to height %s\n"
        // "Set ruler mode\n"
        // "or Set extruder sequence for the entire print"
        // ),
        // "(Shift + G)"
        // ) :
        format(
            _u8L(
                "Jump to height %s\n"
                "or Set ruler mode"
            ),
            "(Shift + G)"
        )
    );
}

void DoubleSliderForLayers::jump_to_value()
{
    // Init "jump to value";
    m_show_get_jump_value_popup = true;
    m_jump_to_value             = m_values[m_ctrl->active_pos()];
    // force dimmed background for jump to value modal popup dialog without animation
    Imgui::disable_background_fadeout_animation();
    process_request_extra_frames();
}

void DoubleSliderForLayers::set_extruder_colors(const std::vector<std::string>& extruder_colors)
{
    m_ticks.colors = extruder_colors;
}

void DoubleSliderForLayers::set_use_default_colors(bool use)
{
    m_ticks.set_use_default_colors(use);
}

bool DoubleSliderForLayers::is_new_print(const std::string& idxs)
{
    if (idxs == "sla" || idxs == m_print_obj_idxs)
        return false;

    m_print_obj_idxs = idxs;
    return true;
}

void DoubleSliderForLayers::show_estimated_times(bool show)
{
    if (m_show_estimated_times_item) {
        m_show_estimated_times_item->set_checked(show);
    } else {
        m_show_estimated_times = show;
    }
}

void DoubleSliderForLayers::show_ruler(bool show)
{
    if (m_show_ruler_item) {
        m_show_ruler_item->set_checked(show);
    } else {
        toggle_show_ruler(show);
    }
}

void DoubleSliderForLayers::show_ruler_background(bool show_bg)
{
    if (m_show_ruler_background_item) {
        m_show_ruler_background_item->set_checked(show_bg);
    } else {
        m_show_ruler_bg = show_bg;
        update_thumbs_border_color();
    }
}

void DoubleSliderForLayers::update_thumbs_border_color()
{
    m_ctrl->set_border_color(
        ImGui::GetColorU32(
            m_show_ruler_bg && m_show_ruler ? ImGuiCol_WindowBg : ImGuiCol_TextDisabled
        )
    );
}

void DoubleSliderForLayers::add_current_tick()
{
    if (
        !can_edit() || m_mode != CustomGCode::Mode::SingleExtruder // ysFIXME-spe3494
    )
        return;

    int tick = m_ctrl->active_pos();
    auto it  = m_ticks.ticks.find(TickCode{tick});

    if (it != m_ticks.ticks.end()) // this tick is already exist
        return;
    if (!m_ticks.check_ticks_changed_event(
            m_mode == CustomGCode::Mode::MultiAsSingle ? CustomGCode::Type::ToolChange :
                                                         CustomGCode::Type::ColorChange,
            m_mode
        ))
    {
        process_ticks_changed();
        return;
    }

    if (m_mode == CustomGCode::Mode::SingleExtruder)
        add_code_as_tick(CustomGCode::Type::ColorChange);
    else {
        m_show_just_color_change_menu = true;
        process_request_extra_frames();
    }
}

void DoubleSliderForLayers::delete_current_tick()
{
    auto it = m_ticks.ticks.find(TickCode{m_ctrl->active_pos()});
    if (it == m_ticks.ticks.end()) // this tick doesn't exist
        return;

    m_ticks.ticks.erase(it);
    update_draw_scroll_line_cb();
    process_ticks_changed();
}

void DoubleSliderForLayers::auto_color_change()
{
    if (!m_ticks.empty()) {
        m_yes_no_cancel_popup.caller  = "auto_color_change";
        m_yes_no_cancel_popup.caption = _u8L("Warning");
        m_yes_no_cancel_popup.txt =
            _u8L("This action will cause deletion of all ticks on vertical slider.")
            + "\n\n"
            + _u8L("This action is not reversible.")
            + "\n"
            + _u8L("Do you want to proceed?");
        m_yes_no_cancel_popup.result = PopupResult::Undefined;
        m_yes_no_cancel_popup.show   = true;
        // force dimmed background for yes no cancel modal popup dialog without animation
        Imgui::disable_background_fadeout_animation();
    } else
        perform_auto_color_change();
}

void DoubleSliderForLayers::perform_auto_color_change()
{
    if (m_cb_auto_color_change != nullptr && m_cb_auto_color_change()) {
        if (m_ticks.empty() && m_cb_notify_empty_auto_color_change != nullptr)
            m_cb_notify_empty_auto_color_change();
    }

    update_draw_scroll_line_cb();
    process_ticks_changed();
}

void DoubleSliderForLayers::process_ticks_changed()
{
    if (m_cb_ticks_changed)
        m_cb_ticks_changed();
    m_revert_btn->set_enabled(!m_ticks.empty());
}

void DoubleSliderForLayers::toggle_show_ruler(bool show)
{
    m_show_ruler        = show;
    const float x_ratio = m_show_ruler ? 7.2f : 6.f;
    m_ctrl->set_min_width(x_ratio * ImGui::GetStyle().FontSizeBase);
    m_ctrl->update_draw_options(m_scale, m_show_ruler);
    update_thumbs_border_color();
}

ImU32 DoubleSliderForLayers::tick_clr() const
{
    return m_show_ruler_bg ? ImU32(m_theme->color_imgui(Platform::Color::Text)) :
                             IM_COL32(255, 255, 255, 255);
}

void DoubleSliderForLayers::extra_render()
{
    if (m_draw_mode == DrawMode::SequentialFffPrint && m_ctrl->is_lclick_on_thumb()) {
        std::string tip = _u8L(
            "The sequential print is on.\n"
            "It's impossible to apply any custom G-code for objects printing sequentually."
        );
        Imgui::tooltip(tip);
    } else
        render_active_ctrl_menu();

    if (m_ctrl->is_rclick_on_thumb()
        && m_mode == CustomGCode::Mode::SingleExtruder // ysFIXME-spe3494
        && can_edit()
        && !m_ticks.has_tick(m_ctrl->active_pos()))
        add_code_as_tick(CustomGCode::Type::ColorChange);

    const ImGuiViewport& viewport = *ImGui::GetMainViewport();

    if (m_show_get_jump_value_popup && render_get_jump_to_value_popup(viewport.GetCenter()))
        process_jump_to_value();

    if (m_pause_print_popup.show && render_pause_print_popup(viewport.GetCenter()))
        process_pause_print();

    if (m_color_picker_popup.show && render_color_picker_popup(viewport.GetCenter()))
        process_color_picker();

    if (m_custom_gcode_popup.show && render_custom_gcode_popup(viewport.GetCenter()))
        process_custom_gcode();

    if (m_yes_no_cancel_popup.show && render_yes_no_cancel_popup(viewport.GetCenter()))
        process_yes_no_cancel();
}

int DoubleSliderForLayers::find_close_layer_idx(const std::vector<float>& zs, float z, float eps)
{
    if (zs.empty())
        return -1;

    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end()) {
        auto it_l = it_h;
        --it_l;
        if (z - *it_l < eps)
            return int(zs.size() - 1);
    } else if (it_h == zs.begin()) {
        if (*it_h - z < eps)
            return 0;
    } else {
        auto it_l = it_h;
        --it_l;
        float dist_l = z - *it_l;
        float dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps)
            return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin());
    }
    return -1;
}

void DoubleSliderForLayers::register_commands(
    MenuManager& menu_manager,
    CommandBindingManager& command_binding_manager
)
{
    if (m_draw_mode != DrawMode::SlaPrint) {
        command_binding_manager.main_command_registry()
            .register_command(
                std::make_unique<Platform::FuncCommand>(
                    "slider-layers-add-current-tick",
                    [this]() { add_current_tick(); },
                    FuncCommandExtraOpts{
                        .keyboard_shortcuts =
                            Platform::KeyboardShortcuts{
                                Platform::KeyboardShortcut{0, Platform::KeyCode::Plus},
                                Platform::KeyboardShortcut{0, Platform::KeyCode::KpPlus}
                            }
                    }
                )
            )
            .register_command(
                std::make_unique<Platform::FuncCommand>(
                    "slider-layers-delete-current-tick",
                    [this]() { delete_current_tick(); },
                    FuncCommandExtraOpts{
                        .keyboard_shortcuts = Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Minus},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::KpMinus}
                        }
                    }
                )
            );
    }

    if (!m_menu_manager) {
        m_menu_manager = &menu_manager;
    }

    if (!m_command_binding_manager) {
        m_command_binding_manager = &command_binding_manager;
    }

    if (!m_cog_menu) {
        create_cog_menu(m_cog_btn);
        update_visibility_cog_menu_items();
    }
}

bool DoubleSliderForLayers::is_wipe_tower_layer(int tick) const
{
    if (!m_ticks.is_wipe_tower || tick >= (int) m_values.size() - 1)
        return false;
    if (tick == 0 || (tick == (int) m_values.size() - 1 && m_values[tick] > m_values[tick - 1]))
        return false;
    if ((m_values[tick - 1] == m_values[tick + 1] && m_values[tick] < m_values[tick + 1])
        || (tick > 0
            && m_values[tick] < m_values[tick - 1])) // if there is just one wiping on the layer
        return true;

    return false;
}

std::string DoubleSliderForLayers::label(int pos, LabelType label_type, unsigned int decimals) const
{
    size_t value = pos;

    if (m_values.empty())
        return format("%1%", pos);
    if (value >= m_values.size())
        return "ErrVal";

    // When "Print Settings -> Multiple Extruders -> No sparse layer" is enabled, then "Smart" Wipe Tower is used for wiping.
    // As a result, each layer with tool changes is splited for min 3 parts: first tool, wiping, second tool ...
    // So, vertical slider have to respect to this case.
    // see https://github.com/prusa3d/PrusaSlicer/issues/6232.
    // m_values contains data for all layer's parts,
    // but m_layers_values contains just unique Z values.
    // Use this function for correct conversion slider position to number of printed layer
    auto get_layer_number = [this](int value, LabelType label_type)
    {
        if (label_type == LabelType::EstimatedTime && m_layers_times.empty())
            return size_t(-1);
        float layer_print_z =
            m_values[is_wipe_tower_layer(value) ? std::max<int>(value - 1, 0) : value];
        auto it = std::lower_bound(
            m_layers_values.begin(),
            m_layers_values.end(),
            layer_print_z - EPSILON
        );
        if (it == m_layers_values.end()) {
            it = std::lower_bound(m_values.begin(), m_values.end(), layer_print_z - EPSILON);
            if (it == m_values.end())
                return size_t(-1);
            return size_t(value);
        }
        return size_t(it - m_layers_values.begin());
    };

    if (label_type == LabelType::EstimatedTime) {
        float time = 0.0f;
        if (m_ticks.is_wipe_tower) {
            size_t layer_number = get_layer_number(int(value), label_type);
            if (layer_number != size_t(-1) && layer_number != m_layers_times.size())
                time = m_layers_times[layer_number];
        } else {
            if (value < m_layers_times.size())
                time = m_layers_times[value];
        }

        return (time == 0.0f) ? "" : format_time_dhms_short_and_splitted(time);
    }
    if (decimals == 2 && m_units == UnitsSystem::Imperial)
        decimals = 4;
    std::string str = convert_and_format_units(
        m_values[value],
        UnitsType::Millimeters,
        (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
        decimals,
        false
    );
    if (label_type == LabelType::Height)
        return str;
    if (label_type == LabelType::HeightWithLayer) {
        size_t layer_number = m_ticks.is_wipe_tower ? get_layer_number(int(value), label_type) + 1 :
                                                      (m_values.empty() ? value : value + 1);
        return format("%1%\n(%2%)", str, layer_number);
    }

    return "";
}

std::string DoubleSliderForLayers::tooltip(int tick /*=-1*/) const
{
    if (m_focus == FocusedItem::None)
        return "";
    if (m_focus == FocusedItem::ColorBand)
        return m_mode != CustomGCode::Mode::SingleExtruder || !can_edit() ?
            "" :
            _u8L("Edit current color - Right click the colored slider segment");
    if (m_focus == FocusedItem::SmartWipeTower)
        return _u8L("This is wipe tower layer");
    if (m_draw_mode == DrawMode::SlaPrint)
        return ""; // no drawn ticks and no tooltips for them in SlaPrinting mode

    std::string tooltip;
    auto tick_code_it = m_ticks.ticks.find(TickCode{tick});

    if (tick_code_it == m_ticks.ticks.end()
        && m_focus == FocusedItem::ActionIcon) // tick doesn't exist
    {
        if (m_draw_mode == DrawMode::SequentialFffPrint)
            return _u8L(
                       "The sequential print is on.\n"
                       "It's impossible to apply any custom G-code for objects printing sequentually."
                   )
                + "\n";

        // Show mode as a first string of tooltop
        tooltip = "    " + _u8L("Print mode") + ": ";
        tooltip +=
            (m_mode == CustomGCode::Mode::SingleExtruder    ? CustomGCodeUtils::SingleExtruderMode :
                 m_mode == CustomGCode::Mode::MultiAsSingle ? CustomGCodeUtils::MultiAsSingleMode :
                                                              CustomGCodeUtils::MultiExtruderMode);
        tooltip += "\n\n";

        /* Note: just on OSX!!!
         * Right click event causes a little scrolling.
         * So, as a workaround we use Ctrl+LeftMouseClick instead of RightMouseClick
         * Show this information in tooltip
         * */

        // Show list of actions with new tick
        tooltip += ( // m_mode == CustomGCode::Mode::MultiAsSingle ?
                       // _u8L("Add extruder change - Left click") :
                       m_mode == CustomGCode::Mode::SingleExtruder ?
                           _u8L(
                               "Add color change - Left click for predefined color or "
                               "Shift + Left click for custom color selection"
                           ) :
                           _u8L("Add color change - Left click")
                   )
            + " "
            + _u8L("or press \"+\" key")
            + "\n"
            + (m_is_osx ? _u8L("Add another code - Ctrl + Left click") :
                          _u8L("Add another code - Right click"));
    }

    if (tick_code_it != m_ticks.ticks.end()) // tick exists
    {
        if (m_draw_mode == DrawMode::SequentialFffPrint)
            return _u8L(
                "The sequential print is on.\n"
                "It's impossible to apply any custom G-code for objects printing sequentually.\n"
                "This code won't be processed during G-code generation."
            );

        // Show custom Gcode as a first string of tooltop
        std::string space = "   ";
        tooltip.clear(); // = space;
        auto format_gcode = [space](std::string gcode) -> std::string
        {
            // when the tooltip is too long, it starts to flicker, see: https://github.com/prusa3d/PrusaSlicer/issues/7368
            // so we limit the number of lines shown
            std::vector<std::string> lines;
            boost::split(lines, gcode, boost::is_any_of("\n"), boost::token_compress_off);
            static const size_t MAX_LINES = 10;
            if (lines.size() > MAX_LINES) {
                gcode = lines.front() + '\n';
                for (size_t i = 1; i < MAX_LINES; ++i) {
                    gcode += lines[i] + '\n';
                }
                gcode += "[" + _u8L("continue") + "]\n";
            }
            boost::replace_all(gcode, "\n", "\n" + space);
            return gcode;
        };
        tooltip += tick_code_it->type == CustomGCode::Type::ColorChange ?
            (m_mode == CustomGCode::Mode::SingleExtruder && tick_code_it->extruder == 1 ?
                 format(_u8L("Color change (\"%1%\")"), gcode(CustomGCode::Type::ColorChange)) :
                 format(
                     _u8L("Color change (\"%1%\") for Extruder %2%"),
                     gcode(CustomGCode::Type::ColorChange),
                     tick_code_it->extruder
                 )) :
            tick_code_it->type == CustomGCode::Type::PausePrint ?
            format(_u8L("Pause print (\"%1%\")"), gcode(CustomGCode::Type::PausePrint)) :
            tick_code_it->type == CustomGCode::Type::Template ?
            format(_u8L("Custom template (\"%1%\")"), gcode(CustomGCode::Type::Template)) :
            tick_code_it->type == CustomGCode::Type::ToolChange ?
            format(_u8L("Extruder (tool) is changed to Extruder \"%1%\""), tick_code_it->extruder) :
            format_gcode(tick_code_it->extra); // tick_code_it->type == Custom

        // If tick is marked as a conflict (exclamation icon),
        // we should to explain why
        ConflictType conflict = m_ticks.is_conflict_tick(*tick_code_it, m_mode, m_values[tick]);
        if (conflict != ConflictType::None)
            tooltip += "\n\n" + _u8L("Note") + "! ";
        if (conflict == ConflictType::ModeConflict)
            tooltip += _u8L(
                "G-code associated to this tick mark is in a conflict with print mode.\n"
                "Editing it will cause changes of Slider data."
            );
        else if (conflict == ConflictType::MeaninglessColorChange)
            tooltip += _u8L(
                "There is a color change for extruder that won't be used till the end of print job.\n"
                "This code won't be processed during G-code generation."
            );
        else if (conflict == ConflictType::MeaninglessToolChange)
            tooltip += _u8L(
                "There is an extruder change set to the same extruder.\n"
                "This code won't be processed during G-code generation."
            );
        else if (conflict == ConflictType::NotPossibleToolChange)
            tooltip += _u8L(
                "There is an extruder change set to a non-existing extruder.\n"
                "This code won't be processed during G-code generation."
            );
        else if (conflict == ConflictType::Redundant)
            tooltip += _u8L(
                "There is a color change for extruder that has not been used before.\n"
                "Check your settings to avoid redundant color changes."
            );

        // Show list of actions with existing tick
        if (m_focus == FocusedItem::ActionIcon)
            tooltip += "\n\n"
                + _u8L("Delete tick mark - Left click or press \"-\" key")
                + "\n"
                + (m_is_osx ? _u8L("Edit tick mark - Ctrl + Left click") :
                              _u8L("Edit tick mark - Right click"));
    }

    return tooltip;
}

void DoubleSliderForLayers::update_draw_scroll_line_cb()
{
    if (m_ticks.empty()
        || m_draw_mode == DrawMode::SequentialFffPrint
        || m_draw_mode == DrawMode::SlaPrint)
        m_ctrl->set_draw_scroll_line_cb(nullptr);
    else
        m_ctrl->set_draw_scroll_line_cb(
            [this](const ImRect& scroll_line, const ImRect& slideable_region)
            { draw_colored_band(scroll_line, slideable_region); }
        );
}

void DoubleSliderForLayers::draw_colored_band(const ImRect& groove, const ImRect& slideable_region)
{
    if (m_ticks.empty() || m_draw_mode == DrawMode::SequentialFffPrint)
        return;

    ImVec2 blank_padding = ImVec2(0.5f * m_ctrl->groove_rect().GetWidth(), 2.0f * m_scale);
    float blank_width    = 1.0f * m_scale;

    ImRect blank_rect = ImRect(
        groove.GetCenter().x - blank_width,
        groove.Min.y,
        groove.GetCenter().x + blank_width,
        groove.Max.y
    );

    ImRect main_band = ImRect(blank_rect);
    main_band.Expand(blank_padding);

    auto draw_band = [this](const ImU32& clr, const ImRect& band_rc)
    {
        ImGui::RenderFrame(band_rc.Min, band_rc.Max, clr, false, band_rc.GetWidth() * 0.5f);
        // cover round corner
        ImGui::RenderFrame(
            ImVec2(band_rc.Min.x, band_rc.Max.y - band_rc.GetWidth() * 0.5f),
            band_rc.Max,
            clr,
            false
        );

        // add tooltip
        m_focus = ImGui::IsMouseHoveringRect(band_rc.Min, band_rc.Max) ? FocusedItem::ColorBand :
                                                                         FocusedItem::None;
    };

    auto draw_main_band = [&main_band](const ImU32& clr)
    { ImGui::RenderFrame(main_band.Min, main_band.Max, clr, false, main_band.GetWidth() * 0.5f); };

    // draw main colored band
    int default_color_idx =
        m_mode == CustomGCode::Mode::MultiAsSingle ? std::max(m_ticks.only_extruder_id - 1, 0) : 0;
    ColorRGBA rgba;
    [[maybe_unused]] bool res = decode_color(m_ticks.colors[default_color_idx], rgba);
    DEBUG_ASSERT(res);
    ImU32 band_clr = Imgui::to_ImU32(rgba);
    draw_main_band(band_clr);

    static float tick_pos;
    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();

    int rclicked_tick = -1;
    while (tick_it != m_ticks.ticks.end()) {
        // get position from tick
        tick_pos = m_ctrl->position_in_rect(tick_it->tick, slideable_region);

        ImRect band_rect = ImRect(
            ImVec2(main_band.Min.x, std::min(tick_pos, main_band.Min.y)),
            ImVec2(main_band.Max.x, std::min(tick_pos, main_band.Max.y))
        );

        if (main_band.Contains(band_rect)) {
            if ((m_mode == CustomGCode::Mode::SingleExtruder
                 && tick_it->type == CustomGCode::Type::ColorChange)
                || (m_mode == CustomGCode::Mode::MultiAsSingle
                    && (tick_it->type == CustomGCode::Type::ToolChange
                        || tick_it->type == CustomGCode::Type::ColorChange)))
            {
                std::string clr_str = m_mode == CustomGCode::Mode::SingleExtruder ?
                    tick_it->color :
                    tick_it->type == CustomGCode::Type::ToolChange ?
                    m_ticks.color_for_tool_change_tick(tick_it) :
                    m_ticks.color_for_color_change_tick(tick_it);

                if (!clr_str.empty()) {
                    ColorRGBA rgba;
                    [[maybe_unused]] bool res = decode_color(clr_str, rgba);
                    DEBUG_ASSERT(res);
                    ImU32 band_clr = Imgui::to_ImU32(rgba);
                    if (tick_it->tick == 0)
                        draw_main_band(band_clr);
                    else {
                        draw_band(band_clr, band_rect);

                        if (ImGui::IsMouseHoveringRect(band_rect.Min, band_rect.Max)
                            && ImGui::GetIO().MouseClicked[1]
                            && !m_ctrl->is_rclick_on_thumb())
                        {
                            rclicked_tick = tick_it->tick;
                        }
                    }
                }
            }
        }
        tick_it++;
    }

    if (m_focus == FocusedItem::ColorBand) {
        if (rclicked_tick > 0)
            edit_tick(rclicked_tick);
        else if (auto tip = tooltip(); !tip.empty())
            Imgui::tooltip(tip, ImGui::GetFontSize() * 20.f);
    }
}

void DoubleSliderForLayers::draw_ticks(const ImRect& slideable_region)
{
    if (m_show_ruler)
        draw_ruler(slideable_region);

    if (m_ticks.empty() || m_draw_mode == DrawMode::SlaPrint)
        return;

    // distance form center           begin  end
    ImVec2 tick_border = ImVec2(23.0f, 2.0f) * m_scale;

    float inner_x  = 11.f * m_scale;
    float outer_x  = 19.f * m_scale;
    float x_center = slideable_region.GetCenter().x;

    float tick_width  = float(int(1.0f * m_scale + 0.5f));
    float icon_offset = 0.5f * m_icon_screen_size;

    ImU32 tick_hovered_clr = tick_clr();

    auto get_tick_pos = [this, slideable_region](int tick)
    { return m_ctrl->position_in_rect(tick, slideable_region); };

    std::set<TickCode>::const_iterator tick_it = m_ticks.ticks.begin();
    bool is_hovered_tick                       = false;
    while (tick_it != m_ticks.ticks.end()) {
        float tick_pos = get_tick_pos(tick_it->tick);

        // draw tick hover box when hovered
        ImRect tick_hover_box = ImRect(
            x_center - tick_border.x,
            tick_pos - tick_border.y,
            x_center + tick_border.x,
            tick_pos + tick_border.y - tick_width
        );

        if (ImGui::IsMouseHoveringRect(tick_hover_box.Min, tick_hover_box.Max)) {
            ImGui::RenderFrame(tick_hover_box.Min, tick_hover_box.Max, tick_hovered_clr, false);
            if (tick_it->type == CustomGCode::Type::ColorChange
                || tick_it->type == CustomGCode::Type::ToolChange)
            {
                m_focus = FocusedItem::Tick;
                Imgui::tooltip(tooltip(tick_it->tick), ImGui::GetFontSize() * 20.f);
            }
            is_hovered_tick = true;
            m_ctrl->set_hovered_region(tick_hover_box);
            if (m_ctrl->is_lclick_on_hovered_pos())
                m_ctrl->is_active_higher_thumb() ? set_higher_pos(tick_it->tick) :
                                                   set_lower_pos(tick_it->tick);
            break;
        }
        ++tick_it;
    }
    if (!is_hovered_tick)
        m_ctrl->invalidate_hovered_region();

    auto active_tick_it = m_ticks.ticks.find(TickCode{m_ctrl->active_pos()});

    tick_it = m_ticks.ticks.begin();
    while (tick_it != m_ticks.ticks.end()) {
        float tick_pos = get_tick_pos(tick_it->tick);

        // draw ticks
        ImRect tick_left(x_center - outer_x, tick_pos - tick_width, x_center - inner_x, tick_pos);
        ImRect tick_right(x_center + inner_x, tick_pos - tick_width, x_center + outer_x, tick_pos);
        ImGui::RenderFrame(tick_left.Min, tick_left.Max, tick_clr(), false);
        ImGui::RenderFrame(tick_right.Min, tick_right.Max, tick_clr(), false);

        ImVec2 icon_pos(
            m_ctrl->ctrl_pos().x + m_ctrl->width() - 0.5f * m_icon_screen_size,
            tick_pos - icon_offset
        );
        std::string btn_label = "tick " + std::to_string(tick_it->tick);

        // draw tick icon-buttons
        bool activate_this_tick = false;
        if (tick_it == active_tick_it && m_allow_editing) {
            // delete tick
            if (render_button(
                    Render::Icon::RemoveTick,
                    Render::Icon::RemoveTickHovered,
                    btn_label,
                    icon_pos,
                    FocusedItem::ActionIcon,
                    tick_it->tick
                ))
            {
                m_ticks.ticks.erase(tick_it);
                process_ticks_changed();
                break;
            }
        } else if (
            m_draw_mode != DrawMode::Regular
        ) // if we have non-regular draw mode, all ticks should be marked with error icon
            activate_this_tick = render_button(
                Render::Icon::ErrorTick,
                Render::Icon::ErrorTickHovered,
                btn_label,
                icon_pos,
                FocusedItem::Tick,
                tick_it->tick
            );
        else if (
            tick_it->type == CustomGCode::Type::ColorChange
            || tick_it->type == CustomGCode::Type::ToolChange
        )
        {
            if (m_ticks.is_conflict_tick(*tick_it, m_mode, m_values[tick_it->tick])
                != ConflictType::None)
                activate_this_tick = render_button(
                    Render::Icon::ErrorTick,
                    Render::Icon::ErrorTickHovered,
                    btn_label,
                    icon_pos,
                    FocusedItem::Tick,
                    tick_it->tick
                );
        } else if (tick_it->type == CustomGCode::Type::PausePrint)
            activate_this_tick = render_button(
                Render::Icon::PausePrint,
                Render::Icon::PausePrintHovered,
                btn_label,
                icon_pos,
                FocusedItem::Tick,
                tick_it->tick
            );
        else
            activate_this_tick = render_button(
                Render::Icon::EditGCode,
                Render::Icon::EditGCodeHovered,
                btn_label,
                icon_pos,
                FocusedItem::Tick,
                tick_it->tick
            );

        if (activate_this_tick) {
            m_ctrl->is_active_higher_thumb() ? set_higher_pos(tick_it->tick) :
                                               set_lower_pos(tick_it->tick);
            break;
        }

        ++tick_it;
    }
}

void DoubleSliderForLayers::draw_ruler(const ImRect& slideable_region)
{
    if (m_values.empty())
        return;

    float step = slideable_region.GetHeight() / float(m_ctrl->max_pos() - m_ctrl->min_pos());

    if (!m_ruler.valid())
        m_ruler.init(m_values, step);

    float inner_x       = 11.f * m_scale;
    float long_outer_x  = 17.f * m_scale;
    float short_outer_x = 14.f * m_scale;
    float tick_width    = float(int(1.0f * m_scale + 0.5f));
    float label_height  = m_icon_screen_size;

    float x_center = slideable_region.GetCenter().x;

    float max_val = 0.0f;
    for (const auto& val : m_ruler.max_values)
        if (max_val < val)
            max_val = val;

    if (m_show_ruler_bg) {
        // draw ruler BG
        ImRect bg_rect = slideable_region;
        bg_rect.Expand(ImVec2(0.f, long_outer_x));
        bg_rect.Min.x -= tick_width;
        bg_rect.Max.x  = m_ctrl->ctrl_pos().x + m_ctrl->width();
        bg_rect.Min.y  = m_ctrl->ctrl_pos().y + label_height;
        bg_rect.Max.y  = m_ctrl->ctrl_pos().y + m_ctrl->height() - label_height;
        ImU32 bg_color = ImGui::GetColorU32(ImGuiCol_WindowBg);
        ImGui::RenderFrame(bg_rect.Min, bg_rect.Max, bg_color, false, 2.f * m_ctrl->rounding());
    }

    auto get_tick_pos = [this, slideable_region](int tick) -> float
    { return m_ctrl->position_in_rect(tick, slideable_region); };

    auto draw_text =
        [max_val, x_center, label_height, long_outer_x, this](const int tick, const float tick_pos)
    {
        ImVec2 start    = ImVec2(x_center + long_outer_x + 1, tick_pos - (0.5f * label_height));
        std::string lbl = label(tick, LabelType::Height, max_val > 100.0f ? 1 : 2);
        ImGui::PushStyleColor(ImGuiCol_Text, tick_clr());
        ImGui::RenderText(start, lbl.c_str());
        ImGui::PopStyleColor();
    };

    auto draw_tick = [this, x_center, tick_width, inner_x](const float tick_pos, const float outer_x)
    {
        ImRect tick_right =
            ImRect(x_center + inner_x, tick_pos - tick_width, x_center + outer_x, tick_pos);
        ImGui::RenderFrame(tick_right.Min, tick_right.Max, tick_clr(), false);
    };

    auto draw_short_ticks =
        [this, short_outer_x, draw_tick, get_tick_pos](float& current_tick, int max_tick)
    {
        if (m_ruler.short_step <= 0.0f)
            return;
        while (current_tick < max_tick) {
            float pos = get_tick_pos(lround(current_tick));
            draw_tick(pos, short_outer_x);
            current_tick += m_ruler.short_step;
            if (current_tick > m_ctrl->max_pos())
                break;
        }
    };

    float short_tick = std::numeric_limits<float>::quiet_NaN();
    int tick         = 0;
    float value      = 0.0f;
    size_t sequence  = 0;
    float prev_y_pos = -1.f;
    int values_size  = int(m_values.size());

    float label_shift = 0.5f * label_height;

    if (m_ruler.long_step < 0) {
        // sequential print when long_step wasn't detected because of a lot of printed objects
        if (m_ruler.max_values.size() > 1) {
            float last_pos = get_tick_pos(m_ctrl->max_pos());
            while (tick <= m_ctrl->max_pos() && sequence < m_ruler.count()) {
                // draw just ticks with max value
                value      = m_ruler.max_values[sequence];
                short_tick = float(tick);

                for (; tick < values_size; tick++) {
                    if (m_values[tick] == value)
                        break;
                    if (m_values[tick] > value) {
                        if (tick > 0)
                            tick--;
                        break;
                    }
                }
                if (tick > m_ctrl->max_pos())
                    break;

                float pos = get_tick_pos(tick);
                draw_tick(pos, long_outer_x);
                if (prev_y_pos < 0.0f
                    || pos == last_pos
                    || (prev_y_pos - pos >= label_shift && pos - last_pos >= label_shift))
                {
                    draw_text(tick, pos);
                    prev_y_pos = pos;
                }
                draw_short_ticks(short_tick, tick);

                sequence++;
                tick++;
            }
        }
        // very short object or some non-trivial ruler with non-regular step (see https://github.com/prusa3d/PrusaSlicer/issues/7263)
        else {
            if (
                step < 1
            ) // step less then 1 px indicates very tall object with non-regular laayer step (probably in vase mode)
                return;
            for (int tick = 1; tick < int(m_values.size()); tick++) {
                float pos = get_tick_pos(tick);
                draw_tick(pos, long_outer_x);
                draw_text(tick, pos);
            }
        }
    } else {
        std::vector<int> last_positions;
        if (m_ruler.count() == 1)
            last_positions.emplace_back(m_ctrl->max_pos());
        else {
            // fill last positions for each object in sequential print
            last_positions.reserve(m_ruler.count());

            int tick        = 0;
            float value     = 0.0f;
            size_t sequence = 0;

            while (tick <= m_ctrl->max_pos()) {
                value += m_ruler.long_step;

                if (sequence < m_ruler.count() && value > m_ruler.max_values[sequence])
                    value = m_ruler.max_values[sequence];

                for (; tick < values_size; tick++) {
                    if (m_values[tick] == value)
                        break;
                    if (m_values[tick] > value) {
                        if (tick > 0)
                            tick--;
                        break;
                    }
                }
                if (tick > m_ctrl->max_pos())
                    break;

                if (sequence < m_ruler.count() && value == m_ruler.max_values[sequence]) {
                    last_positions.emplace_back(tick);
                    value = 0.0f;
                    sequence++;
                    tick++;
                }
            }
        }

        float last_pos = get_tick_pos(last_positions[sequence]);

        while (tick <= m_ctrl->max_pos()) {
            value += m_ruler.long_step;

            if (sequence < m_ruler.count() && value > m_ruler.max_values[sequence])
                value = m_ruler.max_values[sequence];

            short_tick = float(tick);

            for (; tick < values_size; tick++) {
                if (m_values[tick] == value)
                    break;
                if (m_values[tick] > value) {
                    if (tick > 0)
                        tick--;
                    break;
                }
            }
            if (tick > m_ctrl->max_pos())
                break;

            float pos = get_tick_pos(tick);
            draw_tick(pos, long_outer_x);
            if (prev_y_pos < 0.0f
                || pos == last_pos
                || (prev_y_pos - pos >= label_shift && pos - last_pos >= label_shift))
            {
                draw_text(tick, pos);
                prev_y_pos = pos;
            }

            draw_short_ticks(short_tick, tick);

            if (sequence < m_ruler.count() && value == m_ruler.max_values[sequence]) {
                value = 0.0f;
                sequence++;
                tick++;

                if (sequence < m_ruler.count())
                    last_pos = get_tick_pos(last_positions[sequence]);
            }
        }
        // short ticks from the last tick to the end
        draw_short_ticks(short_tick, m_ctrl->max_pos());
    }

    // draw mose move line
    if (m_pos_on_move > 0) {
        float line_pos = get_tick_pos(m_pos_on_move);

        ImRect move_line = ImRect(
            x_center + 0.75f * inner_x,
            line_pos - tick_width,
            x_center + 1.5f * long_outer_x,
            line_pos
        );
        ImGui::RenderFrame(
            move_line.Min,
            move_line.Max,
            m_theme->color_imgui(Platform::Color::AccentTertiary),
            false
        );
        m_pos_on_move = -1;
    }
}

void DoubleSliderForLayers::render_active_ctrl_menu()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f) * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 4.0f * m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1.0f, ImGui::GetStyle().ItemSpacing.y});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * m_scale);

    if (m_ctrl->is_lclick_on_thumb())
        ImGui::OpenPopup("slider_full_menu_popup");
    else if (m_show_just_color_change_menu)
        ImGui::OpenPopup("slider_add_tick_menu_popup");
    else if (m_show_edit_menu)
        ImGui::OpenPopup("edit_menu_popup");

    if (can_edit())
        render_add_tick_menu();
    render_edit_menu();

    ImGui::PopStyleVar(4);

    if (ImGui::GetIO().MouseReleased[0]) {
        m_show_just_color_change_menu = false;
        m_show_edit_menu              = false;
    }
}

void DoubleSliderForLayers::render_edit_menu()
{
    if (!m_show_edit_menu)
        return;

    if (m_ticks.has_tick(m_ctrl->active_pos()) && ImGui::BeginPopup("edit_menu_popup")) {
        std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{m_ctrl->active_pos()});

        if (it->type == CustomGCode::Type::ToolChange) {
            if (render_multi_extruders_menu(true)) {
                ImGui::EndPopup();
                return;
            }
        } else {
            std::string edit_item_name = it->type == CustomGCode::Type::ColorChange ?
                _u8L("Edit color") :
                it->type == CustomGCode::Type::PausePrint ? _u8L("Edit pause print message") :
                                                            _u8L("Edit custom G-code");
            if (Imgui::menu_item_with_icon(edit_item_name.c_str(), "")) {
                edit_tick();
                ImGui::EndPopup();
                return;
            }
        }

        if (it->type == CustomGCode::Type::ColorChange
            && m_mode == CustomGCode::Mode::MultiAsSingle)
        {
            if (render_multi_extruders_menu(true)) {
                ImGui::EndPopup();
                return;
            }
        }

        std::string delete_item_name = it->type == CustomGCode::Type::ColorChange ?
            _u8L("Delete color change") :
            it->type == CustomGCode::Type::ToolChange ? _u8L("Delete tool change") :
            it->type == CustomGCode::Type::PausePrint ? _u8L("Delete pause print") :
                                                        _u8L("Delete custom G-code");
        if (Imgui::menu_item_with_icon(delete_item_name.c_str(), ""))
            delete_current_tick();

        ImGui::EndPopup();
    }
}

bool DoubleSliderForLayers::render_button(
    Render::Icon icon,
    Render::Icon icon_hovered,
    const std::string& label_id,
    const ImVec2& pos,
    FocusedItem focus,
    int tick
)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {m_icon_screen_size, m_icon_screen_size});

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));

    int windows_flag = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin((label_id + "##btn_win").c_str(), nullptr, windows_flag);

    m_focus = focus;

    bool ret             = false;
    Render::Icon icon_id = ImGui::IsWindowHovered() ? icon_hovered : icon;
    ImGui::SetCursorPos({0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.0f, 0.0f, 0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.0f, 0.0f, 0.0f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.0f, 0.0f, 0.0f, 0.0f});
    ImVec2 size(m_icon_screen_size, m_icon_screen_size);
    ret = Imgui::icon_button(icon_id, size);
    ImGui::PopStyleColor(3);
    if (tick > 0
        && tick == m_ctrl->active_pos()
        && ImGui::IsWindowHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        m_show_edit_menu = true;

    std::string tip = m_allow_editing ? tooltip(tick) : "";
    if (!tip.empty() && ImGui::IsItemHovered())
        Imgui::tooltip(tip);

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(5);

    return ret;
}

void DoubleSliderForLayers::render_add_tick_menu()
{
    if (ImGui::BeginPopup("slider_full_menu_popup")) {
        if (m_mode == CustomGCode::Mode::SingleExtruder) {
            if (ImGui::MenuItem(_u8L("Add Color Change").c_str()))
                add_code_as_tick(CustomGCode::Type::ColorChange);
        } else {
            // render_multi_extruders_menu(); // ysFIXME-spe3494
        }

        if (ImGui::MenuItem(_u8L("Add Pause").c_str()))
            add_code_as_tick(CustomGCode::Type::PausePrint);
        if (ImGui::MenuItem(_u8L("Add Custom G-code").c_str()))
            add_code_as_tick(CustomGCode::Type::Custom);
        if (!gcode(CustomGCode::Type::Template).empty()
            && ImGui::MenuItem(_u8L("Add Custom Template").c_str()))
            add_code_as_tick(CustomGCode::Type::Template);

        ImGui::EndPopup();
        return;
    }

    std::string longest_menu_name =
        format(_u8L("Add color change (%1%) for:"), gcode(CustomGCode::Type::ColorChange));

    float label_width        = ImGui::CalcTextSize(longest_menu_name.c_str(), nullptr, true).x;
    ImRect active_thumb_rect = m_ctrl->active_thumb_rect();
    ImVec2 pos               = active_thumb_rect.GetCenter();

    ImGui::SetNextWindowPos(ImVec2(pos.x - label_width - active_thumb_rect.GetWidth(), pos.y));

    if (ImGui::BeginPopup("slider_add_tick_menu_popup")) {
        render_multi_extruders_menu();
        ImGui::EndPopup();
    }
}

bool DoubleSliderForLayers::render_multi_extruders_menu(bool switch_current_code /* = false*/)
{
    return false; // ysFIXME-spe3494
    bool ret = false;

    Palette colors;
    if (m_cb_get_extruder_colors)
        colors = m_cb_get_extruder_colors();

    int extruders_cnt = int(colors.size());

    if (extruders_cnt > 1) {
        int tick = m_ctrl->active_pos();

        if (m_mode == CustomGCode::Mode::MultiAsSingle) {
            std::string menu_name = switch_current_code ? _u8L("Switch code to Change extruder") :
                                                          _u8L("Change extruder");
            if (ImGui::BeginMenu(menu_name.c_str())) {
                std::array<int, 2> active_extruders =
                    m_ticks.active_extruders_for_tick(tick, m_mode);
                for (int i = 1; i <= extruders_cnt; i++) {
                    bool is_active_extruder = i == active_extruders[0] || i == active_extruders[1];
                    std::string item_name   = format(_u8L("Extruder %d"), i);
                    if (is_active_extruder)
                        item_name += " (" + _u8L("active") + ")";

                    ImU32 icon_clr = Imgui::to_ImU32(colors[i - 1]);
                    if (Imgui::menu_item_with_icon(
                            item_name.c_str(),
                            nullptr,
                            icon_clr,
                            false,
                            !is_active_extruder
                        ))
                    {
                        add_code_as_tick(CustomGCode::Type::ToolChange, i);
                        ret = true;
                    }
                }
                ImGui::EndMenu();
            }
        }

        const std::string menu_name = switch_current_code ?
            format(
                _u8L("Switch code to Color change (%1%) for:"),
                gcode(CustomGCode::Type::ColorChange)
            ) :
            format(_u8L("Add color change (%1%) for:"), gcode(CustomGCode::Type::ColorChange));
        if (ImGui::BeginMenu(menu_name.c_str())) {
            std::set<int> used_extruders_for_tick =
                m_ticks.used_extruders_for_tick(tick, m_values[tick]);

            for (int i = 1; i <= extruders_cnt; i++) {
                bool is_used_extruder =
                    used_extruders_for_tick.find(i) != used_extruders_for_tick.end();
                std::string item_name = format(_u8L("Extruder %d"), i);
                if (is_used_extruder)
                    item_name += " (" + _u8L("used") + ")";

                if (ImGui::MenuItem(item_name.c_str())) {
                    add_code_as_tick(CustomGCode::Type::ColorChange, i);
                    ret = true;
                }
            }
            ImGui::EndMenu();
        }
    }
    return ret;
}

bool DoubleSliderForLayers::render_get_jump_to_value_popup(const ImVec2& pos)
{
    if (m_values.empty())
        return false;

    std::string msg_text = _u8L("Enter the height you want to jump to")
        + " ("
        + format_units((m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches)
        + "):";
    std::string win_name = _u8L("Jump to height") + "##btn_win";
    float ctrl_width     = 50.0f;

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();

    ImGui::SetNextWindowPos(pos, ImGuiCond_Once, {0.5f, 0.5f});

    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    bool enter_pressed = false;
    bool ok_pressed    = false;

    // convert to inches if needed
    float value = convert(
        m_jump_to_value,
        UnitsType::Millimeters,
        (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches
    );

    if (!ImGui::IsPopupOpen(win_name.c_str()))
        ImGui::OpenPopup(win_name.c_str());

    if (ImGui::BeginPopupModal(win_name.c_str(), &m_show_get_jump_value_popup, windows_flag)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", msg_text.c_str());
        ImGui::SameLine();
        ImGui::PushItemWidth(ctrl_width);
        std::string mask = (m_units == UnitsSystem::Imperial) ? "%.4f" : "%.2f";
        ImGui::InputFloat(
            "##jump_to",
            &value,
            0.0f,
            0.0f,
            mask.c_str(),
            ImGuiInputTextFlags_CharsDecimal
                | ImGuiInputTextFlags_AutoSelectAll
                | ImGuiInputTextFlags_ParseEmptyRefVal
        );

        // convert back to millimeters if needed
        m_jump_to_value = convert(
            value,
            (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
            UnitsType::Millimeters
        );

        // convert to inches if needed
        float min_pos = convert(
            m_values[m_ctrl->min_pos()],
            UnitsType::Millimeters,
            (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches
        );
        float max_pos = convert(
            m_values[m_ctrl->max_pos()],
            UnitsType::Millimeters,
            (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches
        );
        // check out of range
        bool disable_ok = value < min_pos || value > max_pos;

        ImGui::Separator();
        ImGui::NewLine();
        ImGui::SameLine(
            ImGui::GetCurrentWindow()->Size.x - ImGui::GetStyle().WindowPadding.x - ctrl_width
        );

        if (disable_ok)
            ImGui::BeginDisabled();
        ok_pressed = ImGui::Button("OK##jump_to", {ctrl_width, 0.0f});
        if (disable_ok)
            ImGui::EndDisabled();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_show_get_jump_value_popup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    unified_window_style.pop();

    return enter_pressed || ok_pressed;
}

bool DoubleSliderForLayers::render_pause_print_popup(const ImVec2& pos)
{
    if (m_values.empty())
        return false;

    std::string msg_text =
        _u8L("Enter short message shown on Printer display when a print is paused") + ":";
    std::string win_name =
        _u8L("Message for pausing the print on the current layer")
        + " ("
        + convert_and_format_units(
            m_pause_print_popup.z,
            UnitsType::Millimeters,
            (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
            2
        )
        + ")";

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();

    const ImGuiStyle& style = ImGui::GetStyle();
    // ensure the full caption is visible
    float min_width = ImGui::CalcTextSize(win_name.c_str()).x
        + 2.0f * (style.WindowPadding.x + style.FramePadding.x + style.ItemSpacing.x);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Once, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({min_width, 0.0f}, {FLT_MAX, FLT_MAX});
    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    bool ok_pressed = false;

    std::string str = m_pause_print_popup.data.empty() ?
        "Place bearings in slots and resume printing" :
        m_pause_print_popup.data;

    if (!ImGui::IsPopupOpen(win_name.c_str()))
        ImGui::OpenPopup(win_name.c_str());

    if (ImGui::BeginPopupModal(win_name.c_str(), &m_pause_print_popup.show, windows_flag)) {
        ImGui::Text("%s", msg_text.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText(
            "##pause_print",
            &str,
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_ElideLeft
        );
        m_pause_print_popup.data = str;

        std::string ok_btn     = _u8L("OK");
        std::string cancel_btn = _u8L("Cancel");
        float max_len          = std::max(
            ImGui::CalcTextSize(ok_btn.c_str()).x,
            ImGui::CalcTextSize(cancel_btn.c_str()).x
        );
        float btn_width = max_len + 2.0f * style.FramePadding.x;

        ImGui::Separator();
        ImGui::NewLine();
        ImGui::SameLine(
            ImGui::GetCurrentWindow()->Size.x
            - style.WindowPadding.x
            - style.ItemSpacing.x
            - 2.0f * btn_width
        );

        bool disable_ok = str.empty();
        if (disable_ok)
            ImGui::BeginDisabled();
        ok_pressed = ImGui::Button(ok_btn.c_str(), {btn_width, 0.0f});
        if (disable_ok)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(cancel_btn.c_str(), {btn_width, 0.0f})
            || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_pause_print_popup.show = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    unified_window_style.pop();

    if (!m_pause_print_popup.show) {
        m_pause_print_popup.data    = m_pause_print_popup.cache;
        m_pause_print_popup.editing = false;
    }

    return m_pause_print_popup.show && ok_pressed;
}

bool DoubleSliderForLayers::render_color_picker_popup(const ImVec2& pos)
{
    if (m_values.empty())
        return false;

    std::string win_name = _u8L("Select color for Color Change");

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();

    const ImGuiStyle& style = ImGui::GetStyle();
    // ensure the full caption is visible
    float min_width = ImGui::CalcTextSize(win_name.c_str()).x
        + 2.0f * (style.WindowPadding.x + style.FramePadding.x + style.ItemSpacing.x);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Once, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({min_width, 0.0f}, {FLT_MAX, FLT_MAX});
    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    bool ok_pressed = false;

    if (!ImGui::IsPopupOpen(win_name.c_str()))
        ImGui::OpenPopup(win_name.c_str());

    if (ImGui::BeginPopupModal(win_name.c_str(), &m_color_picker_popup.show, windows_flag)) {
        ColorRGB color;
        decode_color(m_color_picker_popup.data, color);
        float col[4] = {color.r(), color.g(), color.b(), 1.0f};
        ColorRGB ref_color;
        decode_color(m_color_picker_popup.cache, ref_color);
        float ref_col[4] = {ref_color.r(), ref_color.g(), ref_color.b(), 1.0f};
        ImGui::ColorPicker4(
            "##color",
            col,
            ImGuiColorEditFlags_DisplayRGB
                | ImGuiColorEditFlags_NoAlpha
                | ImGuiColorEditFlags_NoLabel
                | ImGuiColorEditFlags_InputRGB,
            ref_col
        );
        color                     = ColorRGB(col[0], col[1], col[2]);
        m_color_picker_popup.data = encode_color(color);

        std::string ok_btn     = _u8L("OK");
        std::string cancel_btn = _u8L("Cancel");
        float max_len          = std::max(
            ImGui::CalcTextSize(ok_btn.c_str()).x,
            ImGui::CalcTextSize(cancel_btn.c_str()).x
        );
        float btn_width = max_len + 2.0f * style.FramePadding.x;

        ImGui::Separator();
        ImGui::NewLine();
        ImGui::SameLine(
            ImGui::GetCurrentWindow()->Size.x
            - style.WindowPadding.x
            - style.ItemSpacing.x
            - 2.0f * btn_width
        );

        ok_pressed = ImGui::Button(ok_btn.c_str(), {btn_width, 0.0f});
        ImGui::SameLine();
        if (ImGui::Button(cancel_btn.c_str(), {btn_width, 0.0f})
            || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_color_picker_popup.show = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    unified_window_style.pop();

    if (!m_color_picker_popup.show) {
        m_color_picker_popup.data    = m_color_picker_popup.cache;
        m_color_picker_popup.editing = false;
    }

    return m_color_picker_popup.show && ok_pressed;
}

bool DoubleSliderForLayers::render_custom_gcode_popup(const ImVec2& pos)
{
    if (m_values.empty())
        return false;

    // used to calculate the correct height of the text input widget
    static float button_bar_height = 0.0f;

    std::string msg_text = _u8L("Enter custom G-code used on current layer") + ":";
    std::string win_name =
        _u8L("Custom G-code on current layer")
        + " ("
        + convert_and_format_units(
            m_custom_gcode_popup.z,
            UnitsType::Millimeters,
            (m_units == UnitsSystem::SI) ? UnitsType::Millimeters : UnitsType::Inches,
            2
        )
        + ")";

    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();

    const ImGuiStyle& style = ImGui::GetStyle();
    // ensure the full caption is visible
    float min_width = ImGui::CalcTextSize(win_name.c_str()).x
        + 2.0f * (style.WindowPadding.x + style.FramePadding.x + style.ItemSpacing.x);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Once, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({min_width, 150.0f}, {FLT_MAX, FLT_MAX});
    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    bool ok_pressed = false;

    std::string str = m_custom_gcode_popup.data;

    if (!ImGui::IsPopupOpen(win_name.c_str()))
        ImGui::OpenPopup(win_name.c_str());

    if (ImGui::BeginPopupModal(win_name.c_str(), &m_custom_gcode_popup.show, windows_flag)) {
        ImGui::Text("%s", msg_text.c_str());

        float h = (button_bar_height == 0.0f) ?
            0.25f * ImGui::GetMainViewport()->Size.y :
            ImGui::GetContentRegionAvail().y - button_bar_height;
        ImGui::InputTextMultiline("##custom_gcode", &str, {-1.0f, h});
        m_custom_gcode_popup.data = str;

        std::string ok_btn     = _u8L("OK");
        std::string cancel_btn = _u8L("Cancel");
        float max_len          = std::max(
            ImGui::CalcTextSize(ok_btn.c_str()).x,
            ImGui::CalcTextSize(cancel_btn.c_str()).x
        );
        float btn_width = max_len + 2.0f * style.FramePadding.x;

        float button_bar_top = ImGui::GetCursorPosY();

        ImGui::Separator();
        ImGui::NewLine();
        ImGui::SameLine(
            ImGui::GetCurrentWindow()->Size.x
            - style.WindowPadding.x
            - style.ItemSpacing.x
            - 2.0f * btn_width
        );

        std::vector<std::string> reserved_tags;
        bool invalid = contains_reserved_tags(str, Biz::libpgcode::RESERVED_TAGS, 1, reserved_tags);
        bool disable_ok = str.empty() || invalid;

        if (disable_ok)
            ImGui::BeginDisabled();
        ok_pressed = ImGui::Button(ok_btn.c_str(), {btn_width, 0.0f});
        if (disable_ok)
            ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(cancel_btn.c_str(), {btn_width, 0.0f})
            || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_custom_gcode_popup.show = false;
            ImGui::CloseCurrentPopup();
        }

        float button_bar_bottom = ImGui::GetCursorPosY();
        button_bar_height       = button_bar_bottom - button_bar_top;

        ImGui::EndPopup();
    }

    unified_window_style.pop();

    if (!m_custom_gcode_popup.show) {
        m_custom_gcode_popup.data    = m_custom_gcode_popup.cache;
        m_custom_gcode_popup.editing = false;
    }

    return m_custom_gcode_popup.show && ok_pressed;
}

bool DoubleSliderForLayers::render_yes_no_cancel_popup(const ImVec2& pos)
{
    Imgui::UnifiedWindowStyle unified_window_style;
    unified_window_style.push();

    const ImGuiStyle& style = ImGui::GetStyle();
    // ensure the full caption is visible
    float min_width = ImGui::CalcTextSize(m_yes_no_cancel_popup.caption.c_str()).x
        + 2.0f * (style.WindowPadding.x + style.FramePadding.x + style.ItemSpacing.x);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Once, {0.5f, 0.5f});
    ImGui::SetNextWindowSizeConstraints({min_width, 0.0f}, {FLT_MAX, FLT_MAX});
    ImGuiWindowFlags windows_flag = ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse;

    bool yes_pressed = false;
    bool no_pressed  = false;

    if (!ImGui::IsPopupOpen(m_yes_no_cancel_popup.caption.c_str()))
        ImGui::OpenPopup(m_yes_no_cancel_popup.caption.c_str());

    if (ImGui::BeginPopupModal(
            m_yes_no_cancel_popup.caption.c_str(),
            &m_yes_no_cancel_popup.show,
            windows_flag
        ))
    {
        ImGui::BeginGroup();
        Imgui::icon_image(Render::Icon::PrusaSlicerIcon, {32.0f, 32.0f});
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::Text("%s", m_yes_no_cancel_popup.txt.c_str());

        std::string yes_btn    = _u8L("Yes");
        std::string no_btn     = _u8L("No");
        std::string cancel_btn = _u8L("Cancel");
        float max_len          = std::max(
            {ImGui::CalcTextSize(yes_btn.c_str()).x,
             ImGui::CalcTextSize(no_btn.c_str()).x,
             ImGui::CalcTextSize(cancel_btn.c_str()).x}
        );
        float btn_width = max_len + 2.0f * style.FramePadding.x;

        ImGui::Separator();
        ImGui::NewLine();
        ImGui::SameLine(
            ImGui::GetCurrentWindow()->Size.x
            - style.WindowPadding.x
            - 2.0f * style.ItemSpacing.x
            - 3.0f * btn_width
        );

        yes_pressed = ImGui::Button(yes_btn.c_str(), {btn_width, 0.0f});
        ImGui::SameLine();
        no_pressed = ImGui::Button(no_btn.c_str(), {btn_width, 0.0f});
        ImGui::SameLine();
        if (ImGui::Button(cancel_btn.c_str(), {btn_width, 0.0f})
            || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_yes_no_cancel_popup.show = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    unified_window_style.pop();

    m_yes_no_cancel_popup.result = PopupResult::Undefined;
    if (yes_pressed)
        m_yes_no_cancel_popup.result = PopupResult::Yes;
    else if (no_pressed)
        m_yes_no_cancel_popup.result = PopupResult::No;
    else if (!m_yes_no_cancel_popup.show)
        m_yes_no_cancel_popup.result = PopupResult::Cancel;

    return m_yes_no_cancel_popup.show && m_yes_no_cancel_popup.result != PopupResult::Undefined;
}

void
DoubleSliderForLayers::add_code_as_tick(CustomGCode::Type type, int selected_extruder /* = -1*/)
{
    int tick = m_ctrl->active_pos();

    if (!m_ticks.check_ticks_changed_event(type, m_mode)) {
        process_ticks_changed();
        return;
    }

    int extruder =
        selected_extruder > 0 ? selected_extruder : std::max<int>(1, m_ticks.only_extruder_id);
    auto it = m_ticks.ticks.find(TickCode{tick});

    bool was_ticks = m_ticks.empty();

    if (it == m_ticks.ticks.end()) {
        if (type == CustomGCode::Type::PausePrint) {
            m_pause_print_popup.data        = "";
            m_pause_print_popup.cache       = "";
            m_pause_print_popup.tick        = -1;
            m_pause_print_popup.extruder_id = extruder;
            m_pause_print_popup.z           = m_values[m_ctrl->active_pos()];
            m_pause_print_popup.editing     = false;
            m_pause_print_popup.show        = true;
            // force dimmed background for pause print modal popup dialog without animation
            Imgui::disable_background_fadeout_animation();
        } else if (type == CustomGCode::Type::ColorChange) {
            if (m_ticks.use_default_colors()) {
                std::string color = m_ticks.color_for_tick(TickCode{tick}, type, extruder);
                m_ticks.add_color_change_tick(tick, color, extruder, m_values[tick]);
                if (was_ticks != m_ticks.empty())
                    update_draw_scroll_line_cb();
                m_show_just_color_change_menu = false;
                process_ticks_changed();
            } else {
                m_color_picker_popup.data        = "#FFFFFF";
                m_color_picker_popup.cache       = "#FFFFFF";
                m_color_picker_popup.tick        = -1;
                m_color_picker_popup.extruder_id = extruder;
                m_color_picker_popup.z           = m_values[m_ctrl->active_pos()];
                m_color_picker_popup.editing     = false;
                m_color_picker_popup.show        = true;
                // force dimmed background for color picker modal popup dialog without animation
                Imgui::disable_background_fadeout_animation();
            }
        } else if (type == CustomGCode::Type::Custom) {
            m_custom_gcode_popup.data        = "";
            m_custom_gcode_popup.cache       = "";
            m_custom_gcode_popup.tick        = -1;
            m_custom_gcode_popup.extruder_id = extruder;
            m_custom_gcode_popup.z           = m_values[m_ctrl->active_pos()];
            m_custom_gcode_popup.editing     = false;
            m_custom_gcode_popup.show        = true;
            // force dimmed background for color picker modal popup dialog without animation
            Imgui::disable_background_fadeout_animation();
        } else if (type == CustomGCode::Type::Template) {
            std::string color = m_ticks.color_for_tick(TickCode{tick}, type, extruder);
            m_ticks.add_template_tick(tick, color, extruder, m_values[tick]);
            if (was_ticks != m_ticks.empty())
                update_draw_scroll_line_cb();
            m_show_just_color_change_menu = false;
            process_ticks_changed();
        } else if (type == CustomGCode::Type::ToolChange) {
            m_ticks.add_tick(tick, type, extruder, m_values[tick]);
            process_ticks_changed();
        } else {
            assert(false);
        }
    } else if (type == CustomGCode::Type::ToolChange || type == CustomGCode::Type::ColorChange) {
        // try to switch tick code to ToolChange or ColorChange accordingly
        if (m_ticks.switch_code_for_tick(it, type, extruder)) {
            if (was_ticks != m_ticks.empty())
                update_draw_scroll_line_cb();
            m_show_just_color_change_menu = false;
            process_ticks_changed();
        }
    }
}

void DoubleSliderForLayers::edit_tick(int tick /* = -1*/)
{
    if (tick < 0)
        tick = m_ctrl->active_pos();
    std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{tick});

    if (it == m_ticks.ticks.end()) // this tick doesn't exist
        return;

    if (!m_ticks.check_ticks_changed_event(it->type, m_mode)) {
        process_ticks_changed();
        return;
    }

    if (it->type == CustomGCode::Type::PausePrint) {
        m_pause_print_popup.data        = it->extra;
        m_pause_print_popup.cache       = it->extra;
        m_pause_print_popup.tick        = it->tick;
        m_pause_print_popup.extruder_id = it->extruder;
        m_pause_print_popup.z           = m_values[it->tick];
        m_pause_print_popup.editing     = true;
        m_pause_print_popup.show        = true;
        // force dimmed background for pause print modal popup dialog without animation
        Imgui::disable_background_fadeout_animation();
    } else if (it->type == CustomGCode::Type::ColorChange) {
        m_color_picker_popup.data        = it->color;
        m_color_picker_popup.cache       = it->color;
        m_color_picker_popup.tick        = it->tick;
        m_color_picker_popup.extruder_id = it->extruder;
        m_color_picker_popup.z           = m_values[it->tick];
        m_color_picker_popup.editing     = true;
        m_color_picker_popup.show        = true;
        // force dimmed background for color picker modal popup dialog without animation
        Imgui::disable_background_fadeout_animation();
    } else if (it->type == CustomGCode::Type::Custom || it->type == CustomGCode::Type::Template) {
        m_custom_gcode_popup.data        = it->extra;
        m_custom_gcode_popup.cache       = it->extra;
        m_custom_gcode_popup.tick        = it->tick;
        m_custom_gcode_popup.extruder_id = it->extruder;
        m_custom_gcode_popup.z           = m_values[it->tick];
        m_custom_gcode_popup.editing     = true;
        m_custom_gcode_popup.show        = true;
        // force dimmed background for custom gcode modal popup dialog without animation
        Imgui::disable_background_fadeout_animation();
    }
}

// discard all custom changes on DoubleSlider
void DoubleSliderForLayers::discard_all_ticks()
{
    clear_ticks();
    m_ctrl->reset_positions();
    update_draw_scroll_line_cb();
    process_ticks_changed();
}

void DoubleSliderForLayers::process_jump_to_value()
{
    if (int tick_value = m_ticks.tick_from_value(m_jump_to_value, true); tick_value > 0) {
        m_show_get_jump_value_popup = false;
        ImGui::CloseCurrentPopup();

        if (m_ctrl->is_active_higher_thumb())
            set_higher_pos(tick_value);
        else
            set_lower_pos(tick_value);
    }
}

void DoubleSliderForLayers::process_pause_print()
{
    assert(!m_pause_print_popup.data.empty());

    if (m_pause_print_popup.editing) {
        assert(m_pause_print_popup.tick >= 0);
        std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{m_pause_print_popup.tick});
        assert(it != m_ticks.ticks.end());
        m_ticks.edit_tick(it, m_pause_print_popup.z, m_pause_print_popup.data);
        m_pause_print_popup.editing = false;
    } else {
        bool was_ticks = m_ticks.empty();

        int tick = m_ctrl->active_pos();
        m_ticks.add_pause_print_tick(
            tick,
            m_pause_print_popup.data,
            m_pause_print_popup.extruder_id,
            m_values[tick]
        );

        if (was_ticks != m_ticks.empty())
            update_draw_scroll_line_cb();
    }

    m_show_just_color_change_menu = false;
    process_ticks_changed();

    m_pause_print_popup.show = false;
    ImGui::CloseCurrentPopup();
}

void DoubleSliderForLayers::process_color_picker()
{
    assert(!m_color_picker_popup.data.empty());

    if (m_color_picker_popup.editing) {
        assert(m_color_picker_popup.tick >= 0);
        std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{m_color_picker_popup.tick});
        assert(it != m_ticks.ticks.end());
        m_ticks.edit_tick(it, m_color_picker_popup.z, m_color_picker_popup.data);
        m_color_picker_popup.editing = false;
    } else {
        bool was_ticks = m_ticks.empty();

        int tick = m_ctrl->active_pos();
        m_ticks.add_color_change_tick(
            tick,
            m_color_picker_popup.data,
            m_color_picker_popup.extruder_id,
            m_values[tick]
        );

        if (was_ticks != m_ticks.empty())
            update_draw_scroll_line_cb();
    }

    m_show_just_color_change_menu = false;
    process_ticks_changed();

    m_color_picker_popup.show = false;
    ImGui::CloseCurrentPopup();
}

void DoubleSliderForLayers::process_custom_gcode()
{
    assert(!m_custom_gcode_popup.data.empty());

    if (m_custom_gcode_popup.editing) {
        assert(m_custom_gcode_popup.tick >= 0);
        std::set<TickCode>::iterator it = m_ticks.ticks.find(TickCode{m_custom_gcode_popup.tick});
        assert(it != m_ticks.ticks.end());
        m_ticks.edit_tick(it, m_custom_gcode_popup.z, m_custom_gcode_popup.data);
        m_custom_gcode_popup.editing = false;
    } else {
        bool was_ticks = m_ticks.empty();

        int tick = m_ctrl->active_pos();
        m_ticks.add_custom_gcode_tick(
            tick,
            m_custom_gcode_popup.data,
            m_custom_gcode_popup.extruder_id,
            m_values[tick]
        );

        if (was_ticks != m_ticks.empty())
            update_draw_scroll_line_cb();
    }

    m_show_just_color_change_menu = false;
    process_ticks_changed();

    m_custom_gcode_popup.show = false;
    ImGui::CloseCurrentPopup();
}

void DoubleSliderForLayers::process_yes_no_cancel()
{
    assert(
        m_yes_no_cancel_popup.result == PopupResult::Yes
        || m_yes_no_cancel_popup.result == PopupResult::No
    );

    if (m_yes_no_cancel_popup.result == PopupResult::Yes) {
        if (m_yes_no_cancel_popup.caller == "auto_color_change") {
            clear_ticks();
            perform_auto_color_change();
        }
    }

    m_yes_no_cancel_popup.show = false;
    ImGui::CloseCurrentPopup();
}

} // namespace Slic3r::App::Preview
