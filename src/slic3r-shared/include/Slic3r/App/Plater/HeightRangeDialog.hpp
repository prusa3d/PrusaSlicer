#pragma once

#include "Slic3r/App/Plater/HeightRangeRow.hpp"
#include "Slic3r/App/Plater/GizmoWindowWithLeftSidePanel.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/LayerHeightProfile.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::Domain {
struct ConfigBox;
} // namespace Slic3r::Domain

namespace Slic3r::App::Yoga {
class InputText;
class LayoutButton;
class Rectangle;
class ScrollArea;
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class LayerRangeSettingsDialog;
class HeightRangeControl;

class HeightRangeDialog : public GizmoWindowWithLeftSidePanel
{
public:
    struct Callbacks
    {
        std::function<void()> add_range_clicked = []() {};
        std::function<void()> revert_clicked    = []() {};
        std::function<void(const Domain::LayerHeightRange& range_to_delete)> delete_range_clicked =
            [](const Domain::LayerHeightRange&) {};
        std::function<void(const Domain::LayerHeightRange& range_to_select)> height_range_selected =
            [](const Domain::LayerHeightRange&) {};
        std::function<void()> height_range_deselected = []() {};
        std::function<void(double)> min_z_changed     = [](double) {};
        std::function<void(double)> max_z_changed     = [](double) {};
        std::function<void(const std::string& removed_override_name)> override_removed =
            [](const std::string&) {};
        std::function<void(const Domain::LayerHeightRange& range_to_undo)> undo_overrides_clicked =
            [](const Domain::LayerHeightRange&) {};
        std::function<
            void(const Domain::LayerHeightRange& range_to_drag, double new_min_z, double new_max_z)>
            height_range_dragging = [](const Domain::LayerHeightRange&, double, double) {};
        std::function<
            void(const Domain::LayerHeightRange& dragged_range, double new_min_z, double new_max_z)>
            height_range_drag_ended = [](const Domain::LayerHeightRange&, double, double) {};
        std::function<void(std::optional<Domain::LayerHeightRange> hovered_range)>
            height_range_hovered = [](std::optional<Domain::LayerHeightRange>) {};
    };

    HeightRangeDialog() = delete;

    explicit HeightRangeDialog(Biz::IConfigBoxSetter* config_box_setter);

    ~HeightRangeDialog() override = default;

    Callbacks& callbacks();

    void set_layer_height_title(double layer_height);
    void set_object_max_z(float object_max_z);
    void set_min_layer_height(float min_layer_height);
    void set_max_layer_height(float max_layer_height);
    void set_default_layer_height(float default_layer_height);
    void set_layer_height_profile(const Domain::ZHeightPairs& layer_height_profile);
    void set_selected_height_range_config_box(Domain::ConfigBox* settings);

    void set_height_range_min_z(double min_z);
    void set_height_range_max_z(double max_z);

    void update_height_ranges(
        const HeightRangeEntries& height_range_entries,
        const Domain::LayerConfigRanges& layer_config_ranges
    );
    void update_overrides_section();
    void update_single_height_range(
        const Domain::LayerHeightRange& range_to_update,
        const HeightRangeEntry& height_range_entry
    );

    void select_range(const Domain::LayerHeightRange& range_to_select);
    void clear_selection();
    void clear_overrides();

    void highlight_range(const std::optional<Domain::LayerHeightRange>& range_to_highlight);

private:
    Biz::IConfigBoxSetter* m_config_box_setter               = nullptr;
    HeightRangeControl* m_layer_height_profile_control = nullptr;
    LayerRangeSettingsDialog* m_layer_range_settings_dialog  = nullptr;
    Domain::ConfigBox* m_current_range_settings              = nullptr;

    Yoga::Item* m_height_range_list_container = nullptr;
    std::vector<HeightRangeRow*> m_height_range_rows;

    Yoga::Item* m_height_range_editor_section = nullptr;
    Yoga::InputText* m_min_z_input            = nullptr;
    Yoga::InputText* m_max_z_input            = nullptr;

    Yoga::Item* m_overrides_section           = nullptr;
    Yoga::Item* m_add_modifier_button_section = nullptr;

    std::optional<Domain::LayerHeightRange> m_selected_height_range;
    std::optional<Domain::LayerHeightRange> m_dragged_height_range;
    std::optional<size_t> m_selected_row_index;
    std::optional<size_t> m_highlighted_row_index;

    Callbacks m_callbacks;

    void add_background_deselection_catcher(Item* parent);
    void add_height_range_section(Item* parent);
    void add_height_range_editor_section(Item* parent);
    void add_overrides_section(Item* parent);
    void add_modifier_button_section(Item* parent);
    void add_override_category_section(
        Item* parent,
        Domain::ConfigItemDef::Category category,
        const std::vector<std::reference_wrapper<const Domain::ConfigItem>>& config_items
    );
    void add_help_section(Item* parent);

    void init_layer_height_profile_control();

    void select_range(const std::optional<Domain::LayerHeightRange>& range_to_select);

    std::optional<size_t> find_height_range_row_index(const Domain::LayerHeightRange& key) const;
};

} // namespace Slic3r::App::Plater
