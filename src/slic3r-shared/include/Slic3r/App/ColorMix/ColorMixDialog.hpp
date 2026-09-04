#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"

#include "Slic3r/Biz/Algorithms/VirtualExtruderPresets.hpp"
#include "Slic3r/Biz/IColorsChangedListener.hpp"
#include "Slic3r/Biz/ISelectedConfigContainerChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/VirtualExtruder.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class ProjectSettingsInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {
class LogicalPrinterSettingsDialog;
class Navigator;
} // namespace Slic3r::App

namespace Slic3r::App::Yoga {
class ColorDropdown;
class ColorPickerButton;
class LayoutButton;
class ScrollArea;
class StackLayout;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::ColorMix {

class BarycentricRatioPicker;
class BlendRatioBar;
class ColorMixRecipeRow;
class LayerSequenceBar;

/**
 * @brief Editor of virtual extruders.
 *
 * All changes go into a working copy and are saved through the ProjectInteractor only when the
 * user confirms.
 */
class ColorMixDialog :
    public Yoga::Dialog,
    public Biz::IColorsChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::ISelectedConfigContainerChangedListener
{
public:
    ColorMixDialog(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        LogicalPrinterSettingsDialog* logical_printer_settings_dialog
    );

    void update_from_project();

    void on_colors_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const std::vector<Domain::ColorRGB>& colors
    ) override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_selected_config_container_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

protected:
    void close_action() override;

private:
    using BlendPreset = Biz::Algorithms::VirtualExtruderPresets::BlendPreset;

    void on_about_to_show() override;

    void create_left_panel(Yoga::Item* parent);
    void create_right_panel(Yoga::Item* parent);
    void create_presets_pane(Yoga::Item* pane);
    void create_editor_pane(Yoga::Item* pane);
    void create_footer(Yoga::Item* parent);

    void update_physical_colors();
    void rebuild_virtual_extruder_list(int select_index);
    void select_row(int index);
    void populate_editor_from_selection();
    void update_preview_and_validation();
    void update_right_panel_visibility();
    void rebuild_component_rows();
    void rebuild_filter_buttons();
    void rebuild_preset_grid();
    void update_selected_list_row();
    void clear_editor_rows();

    Yoga::ColorDropdown* create_extruder_dropdown(
        Yoga::Item* parent,
        unsigned int extruder_id_1based,
        const std::function<void(unsigned int extruder_id_1based)>& on_selected
    );

    void on_row_clicked(int index);
    void on_row_remove_clicked(int index);
    void on_add_blend_clicked();
    void on_add_or_remove_component_clicked();
    void on_remove_component(size_t component_index);
    void on_composition_changed();
    void on_preset_clicked(const BlendPreset& preset);
    void on_accept_clicked();

    Biz::Algorithms::VirtualExtruderPresets::PhysicalExtruderSlots preset_slots() const;

    /**
     * @brief Lowest id free in every printer group of the project.
     */
    unsigned int next_free_virtual_id() const;

    int selected_index() const;

    /**
     * @brief Index of the recipe matching the preset, or -1 when there is none.
     */
    int find_matching_blend_index(const BlendPreset& preset) const;
    int count_objects_using(unsigned int virtual_extruder_id) const;
    ImColor physical_color(unsigned int extruder_id_1based) const;
    void clear_color_override();

    /**
     * @brief First physical extruder the recipe does not use yet.
     */
    unsigned int pick_unused_physical_id(const Domain::VirtualExtruder& virtual_extruder) const;

    Biz::ListenerScope<Biz::IColorsChangedListener, Biz::ProjectSettingsInteractor, ColorMixDialog>
        m_colors_changed_listener_scope;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        ColorMixDialog>
        m_preset_changed_listener_scope;

    Biz::ListenerScope<
        Biz::ISelectedConfigContainerChangedListener,
        Biz::ProjectInteractor,
        ColorMixDialog>
        m_selected_config_container_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    LogicalPrinterSettingsDialog* m_logical_printer_settings_dialog{nullptr};

    Domain::SelectionId m_target_project_id{Domain::INVALID_ID};
    Domain::SelectionId m_target_config_container_id{Domain::INVALID_ID};

    unsigned int m_physical_extruder_count{0};
    std::vector<std::string> m_physical_colors;
    std::vector<std::string> m_physical_types;
    std::map<unsigned int, int> m_object_use_counts;
    Domain::VirtualExtruders m_working_virtual_extruders;
    std::vector<unsigned int> m_original_ids;
    std::vector<unsigned int> m_removed_ids;
    std::set<unsigned int> m_reserved_virtual_ids;
    unsigned int m_max_physical_slot_count{0};
    std::vector<bool> m_preset_extruders_enabled;
    int m_selected_index{-1};

    Yoga::Text* m_few_extruders_hint{nullptr};
    Yoga::Item* m_list_container{nullptr};
    std::vector<ColorMixRecipeRow*> m_recipe_rows;
    Yoga::LayoutButton* m_add_blend_button{nullptr};

    Yoga::StackLayout* m_right_stack{nullptr};
    Yoga::Item* m_presets_pane{nullptr};
    Yoga::Item* m_editor_pane{nullptr};

    Yoga::Item* m_filter_button_row{nullptr};
    Yoga::Text* m_two_color_presets_title{nullptr};
    Yoga::Item* m_two_color_presets_wrap{nullptr};
    Yoga::Text* m_three_color_presets_title{nullptr};
    Yoga::Item* m_three_color_presets_wrap{nullptr};
    Yoga::Text* m_presets_empty_hint{nullptr};

    Yoga::Text* m_editor_title{nullptr};
    Yoga::Text* m_editor_description{nullptr};
    Yoga::Item* m_editor_rows{nullptr};
    Yoga::LayoutButton* m_add_component_button{nullptr};
    Yoga::ColorPickerButton* m_color_picker_button{nullptr};
    Yoga::LayoutButton* m_reset_display_color_button{nullptr};
    LayerSequenceBar* m_sequence_bar{nullptr};

    std::vector<Yoga::ColorDropdown*> m_component_dropdowns;
    std::vector<Yoga::Text*> m_component_percent_texts;
    BlendRatioBar* m_inline_ratio_bar{nullptr};
    BarycentricRatioPicker* m_inline_triangle{nullptr};
};

} // namespace Slic3r::App::ColorMix
