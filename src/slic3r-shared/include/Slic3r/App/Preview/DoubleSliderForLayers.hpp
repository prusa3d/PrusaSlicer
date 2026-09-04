#pragma once

#include "Slic3r/App/Imgui/DoubleSlider.hpp"
#include "Slic3r/App/Imgui/RulerForDoubleSlider.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"

#include "TickCodeManager.hpp"
#include "Types.hpp"

namespace Slic3r::App::Yoga {
class LayoutButton;
class Menu;
class MenuItem;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Preview {

enum class FocusedItem
{
    None,
    ColorBand,
    ActionIcon,
    SmartWipeTower,
    Tick
};

enum class DrawMode
{
    Regular,
    SlaPrint,
    SequentialFffPrint
};

enum class LabelType
{
    HeightWithLayer,
    Height,
    EstimatedTime,
};

class DoubleSliderForLayers : public Imgui::DoubleSlider::Manager<float>
{
public:
    explicit DoubleSliderForLayers();

    void change_one_layer_lock();

    Domain::CustomGCode::Info ticks_values() const;
    void set_ticks_values(const Domain::CustomGCode::Info& custom_gcode_per_print_z);

    void set_layers_times(const std::vector<float>& layers_times, float total_time);
    void set_layers_times(const std::vector<float>& layers_times);

    void enable_editing(bool enable) { m_allow_editing = enable; }
    void set_draw_mode(bool is_sla_print, bool is_sequential_print);

    void set_mode_and_only_extruder(const bool is_one_extruder_printed_model, const int only_extruder);

    void force_ruler_update() { m_ruler.invalidate(); }

    // jump to selected layer
    void jump_to_value();

    // just for editor

    void set_extruder_colors(const std::vector<std::string>& extruder_colors);
    void set_use_default_colors(bool use);
    bool is_new_print(const std::string& print_obj_idxs);
    void show_estimated_times(bool show);
    void show_ruler(bool show);
    void show_ruler_background(bool show_bg);

    void seq_top_layer_only(bool show)
    {
        m_seq_top_layer_only = show;
    }

    // manipulation with slider from keyboard

    // add default action for tick, when press "+"
    void add_current_tick();
    // delete current tick, when press "-"
    void delete_current_tick();

    bool has_ticks() const { return !m_ticks.empty(); }
    void clear_ticks() { m_ticks.ticks.clear(); }

    bool is_auto_color_change_completed() const {
        // allow max 3 auto color changes
        return m_ticks.ticks.size() > 2;
    }

    void set_ticks_changed_callback(TicksChangedCallback cb) { m_cb_ticks_changed = cb; };
    void set_app_config_changed_callback(AppConfigChangedCallback cb) { m_cb_app_config_changed = cb; }
    void set_get_extruder_colors_callback(GetExtruderColorsCallback cb) { m_cb_get_extruder_colors = cb; }
    void set_auto_color_change_callback(AutoColorChangeCallback cb) { m_cb_auto_color_change = cb; }
    void set_notify_empty_auto_color_change_callback(NotifyEmptyAutoColorChangeCallback cb) { m_cb_notify_empty_auto_color_change = cb; }
    void set_notify_empty_color_change_gcode_callback(NotifyEmptyColorChangeGCodeCallback cb) { m_ticks.set_notify_empty_color_change_gcode_callback(cb); }
    void set_show_info_msg_callback(ShowInfoMsgCallback cb) { m_ticks.set_show_info_msg_callback(cb); }
    void set_get_gcode_callback(GetGCodeCallback cb) { m_ticks.set_get_gcode_callback(cb); }
    void set_get_used_extruders_in_print_callback(GetUsedExtrudersInPrintCallback cb) { m_ticks.set_get_used_extruders_in_print_callback(cb); }
    void set_get_extruders_sequence_callback(GetExtrudersSequenceCallback cb) { m_ticks.set_get_extruders_sequence_callback(cb); }

    std::string gcode(Domain::CustomGCode::Type type) const { return m_ticks.gcode(type); }

    const ImVec2 size() const { return m_size; }

    void set_units(Biz::libpgcode::UnitsSystem units) { m_units = units; }

    static int find_close_layer_idx(const std::vector<float>& zs, float z, float eps);

    void register_commands(
        MenuManager& menu_manager,
        CommandBindingManager& command_binding_manager
    ) override;

private:
    bool is_wipe_tower_layer(int tick) const;

    std::string label(int tick, LabelType label_type, unsigned int decimals = 2) const;

    std::string tooltip(int tick = -1) const;

    void update_thumbs_border_color();

    void update_draw_scroll_line_cb();

    // functions for extend rendering of m_ctrl

    void extra_render();

    void create_cog_menu(Item* parent);

    void update_visibility_cog_menu_items();

    void draw_colored_band(const ImRect& groove, const ImRect& slideable_region);
    void draw_ticks(const ImRect& slideable_region);
    void draw_ruler(const ImRect& slideable_region);
    void render_active_ctrl_menu();
    void render_edit_menu();
    bool render_button(
        Render::Icon icon,
        Render::Icon icon_hovered,
        const std::string& label_id,
        const ImVec2& pos,
        FocusedItem focus,
        int tick = -1
    );
    void render_add_tick_menu();
    bool render_multi_extruders_menu(bool switch_current_code = false);

    bool render_get_jump_to_value_popup(const ImVec2& pos);
    bool render_pause_print_popup(const ImVec2& pos);
    bool render_color_picker_popup(const ImVec2& pos);
    bool render_custom_gcode_popup(const ImVec2& pos);
    bool render_yes_no_cancel_popup(const ImVec2& pos);

    void auto_color_change();
    void perform_auto_color_change();

    void add_code_as_tick(Domain::CustomGCode::Type type, int selected_extruder = -1);
    void edit_tick(int tick = -1);
    void discard_all_ticks();

    void process_jump_to_value();
    void process_pause_print();
    void process_color_picker();
    void process_custom_gcode();
    void process_yes_no_cancel();

    bool can_edit() const { return m_allow_editing && m_draw_mode != DrawMode::SlaPrint; }

    /**
     * @name Implementation of Imgui::DoubleSlider::Manager private interface
     * @{
     */
    std::string label(int pos) const override { return label(pos, LabelType::HeightWithLayer); }
    /**@}*/

    void process_ticks_changed();
    void toggle_show_ruler(bool show);
    ImU32 tick_clr() const;

private:
    Biz::libpgcode::UnitsSystem m_units{ Biz::libpgcode::UnitsSystem::SI };

    bool m_is_osx{ false };
    bool m_allow_editing{ true };
    bool m_show_estimated_times{ true };
    bool m_show_ruler{ false };
    bool m_show_ruler_bg{ true };
    bool m_show_edit_menu{ false };
    bool m_seq_top_layer_only{ false };
    int m_pos_on_move{ -1 };

    DrawMode m_draw_mode{ DrawMode::Regular };
    Domain::CustomGCode::Mode m_mode{ Domain::CustomGCode::Mode::SingleExtruder };
    FocusedItem m_focus{ FocusedItem::None };

    Imgui::DoubleSlider::Ruler m_ruler;
    TickCodeManager m_ticks;
    float m_icon_screen_size{ 18.0f };
    ImVec2 m_size{ 0.0f, 0.0f };

    std::vector<float> m_layers_times;
    std::vector<float> m_layers_values;

    float m_jump_to_value{ 0.0f };

    Yoga::LayoutButton* m_revert_btn{ nullptr };
    Yoga::LayoutButton* m_lock_btn{ nullptr };
    Yoga::LayoutButton* m_cog_btn{ nullptr };

    Yoga::Menu* m_cog_menu{nullptr};

    Yoga::MenuItem* m_edit_extruder_sequence_menu_item{ nullptr };
    Yoga::MenuItem* m_seq_top_layer_only_item{ nullptr };
    Yoga::MenuItem* m_use_default_colors_menu_item{ nullptr };
    Yoga::MenuItem* m_auto_color_change_menu_item{ nullptr };

    Yoga::MenuItem* m_show_estimated_times_item{ nullptr };
    Yoga::MenuItem* m_show_ruler_item{ nullptr };
    Yoga::MenuItem* m_show_ruler_background_item{ nullptr };

    struct TickPopup
    {
        std::string data;
        std::string cache;
        int extruder_id{0};
        int tick{-1};
        float z{0.0f};
        bool editing{false};
        bool show{false};
    };

    TickPopup m_pause_print_popup;
    TickPopup m_color_picker_popup;
    TickPopup m_custom_gcode_popup;

    enum class PopupResult
    {
        Undefined,
        Yes,
        No,
        Cancel
    };

    struct YesNoCancelPopup
    {
        std::string caller;
        std::string caption;
        std::string txt;
        bool show{ false };
        PopupResult result{ PopupResult::Undefined };
    };
    YesNoCancelPopup m_yes_no_cancel_popup;

    bool m_show_just_color_change_menu{ false };
    bool m_show_get_jump_value_popup{ false };

    std::string m_print_obj_idxs;

    TicksChangedCallback m_cb_ticks_changed{ nullptr };
    GetExtruderColorsCallback m_cb_get_extruder_colors{ nullptr };
    AutoColorChangeCallback m_cb_auto_color_change{ nullptr };
    AppConfigChangedCallback m_cb_app_config_changed{ nullptr };
    NotifyEmptyAutoColorChangeCallback m_cb_notify_empty_auto_color_change{ nullptr };
};

} // namespace Slic3r::App::Preview
