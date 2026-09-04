#include "Slic3r/App/ColorMix/ColorMixDialog.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/ColorDropdown.hpp"
#include "Slic3r/App/ColorMix/BarycentricRatioPicker.hpp"
#include "Slic3r/App/ColorMix/BlendRatioBar.hpp"
#include "Slic3r/App/ColorMix/BlendRecipeTile.hpp"
#include "Slic3r/App/ColorMix/ColorMixRecipeRow.hpp"
#include "Slic3r/App/ColorMix/ColorMixUtils.hpp"
#include "Slic3r/App/ColorMix/ExtruderFilterButton.hpp"
#include "Slic3r/App/ColorMix/LayerSequenceBar.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/LogicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/ColorPickerButton.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/Algorithms/Color.hpp"
#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Preset/SelectedPresetConfigPack.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using Slic3r::App::Platform::Color;
using Slic3r::App::Platform::ColorGroup;
using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::Preset::PresetItemType;
using Slic3r::Domain::ColorRGB;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigItem;
using Slic3r::Domain::FilamentSettings;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::VirtualExtruder;
using Slic3r::Domain::VirtualExtruderComponent;
using Slic3r::Domain::VirtualExtruders;
using Slic3r::Domain::Preset::SelectedPresetConfigPack;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::ColorMix {

using Slic3r::Biz::Algorithms::VirtualExtruderPresets::BlendPreset;
using Slic3r::Biz::Algorithms::VirtualExtruderPresets::BlendPresetGroups;
using Slic3r::Biz::Algorithms::VirtualExtruderPresets::BlendPresets;
using Slic3r::Biz::Algorithms::VirtualExtruderPresets::PhysicalExtruderSlots;

namespace {

const constexpr Unit PANEL_GAP{8_fpx};
const constexpr Unit LEFT_PANEL_WIDTH{330_fpx};
const constexpr Unit EXTRUDER_DROPDOWN_WIDTH{150_fpx};
const constexpr Unit PERCENT_FONT_SIZE{1.6_rem};
constexpr const char* FALLBACK_COLOR = "#808080";

struct ColorMixDialogInput
{
    unsigned int physical_extruder_count{0};
    std::vector<std::string> physical_colors;
    std::vector<std::string> physical_types;
    VirtualExtruders virtual_extruders;
    std::map<unsigned int, int> object_use_counts;
    std::set<unsigned int> reserved_virtual_ids;
    unsigned int max_physical_slot_count{0};
};

ColorMixDialogInput collect_color_mix_dialog_input(ProjectInteractor& project_interactor)
{
    ColorMixDialogInput input;

    const SelectionId config_container_id = project_interactor.selected_config_container_id();
    if (config_container_id == Domain::INVALID_ID) {
        return input;
    }

    const Project& project                  = project_interactor.selected_project();
    const ConfigContainer& config_container = project_interactor.selected_config_container();

    input.physical_extruder_count = static_cast<unsigned int>(
        config_container.selected_preset().hw_config.material_slot_count()
    );
    input.virtual_extruders = config_container.virtual_extruders();

    const std::vector<ColorRGB> slot_colors =
        project_interactor.project_settings_interactor().get_colors(config_container_id);
    const SelectedPresetConfigPack preset_config_pack(config_container.selected_preset());

    input.physical_colors.reserve(input.physical_extruder_count);
    input.physical_types.reserve(input.physical_extruder_count);
    for (unsigned int slot_index = 0; slot_index < input.physical_extruder_count; ++slot_index) {
        input.physical_colors.push_back(
            slot_index < slot_colors.size() ?
                Algorithms::Color::encode_color(slot_colors[slot_index]) :
                std::string{}
        );

        std::string filament_type;
        if (slot_index < preset_config_pack.filament_size()) {
            const FilamentSettings& filament = preset_config_pack.get_filament(slot_index);
            if (const ConfigItem* filament_type_item = filament.items.find("filament_type");
                filament_type_item != nullptr)
            {
                filament_type = filament_type_item->get<std::string>();
            }
        }

        input.physical_types.push_back(std::move(filament_type));
    }

    // Counted up front so that the user can be warned before deleting a recipe still in use.
    const auto count_extruder_use = [&input](int extruder_id)
    {
        if (extruder_id > static_cast<int>(input.physical_extruder_count)) {
            ++input.object_use_counts[static_cast<unsigned int>(extruder_id)];
        }
    };

    for (const ModelObject* object : project.model().objects) {
        if (object == nullptr) {
            continue;
        }

        count_extruder_use(object->object_settings.items.opt("extruder").get<int>());

        for (const ModelVolume* volume : object->volumes) {
            if (volume == nullptr) {
                continue;
            }

            if (const std::optional<ConfigItem> volume_extruder =
                    volume->volume_settings.overrides.get("extruder");
                volume_extruder.has_value())
            {
                count_extruder_use(volume_extruder->get<int>());
            }

            if (!volume->is_mm_painted()) {
                continue;
            }

            const std::vector<bool>& used_states =
                volume->mm_segmentation_facets.get_data().used_states;
            for (const VirtualExtruder& virtual_extruder : input.virtual_extruders) {
                if (virtual_extruder.id < used_states.size() && used_states[virtual_extruder.id]) {
                    ++input.object_use_counts[virtual_extruder.id];
                }
            }
        }
    }

    // New ids must be free in every printer group, because the facet paint is shared.
    for (const std::unique_ptr<ConfigContainer>& other_container : project.config_containers()) {
        for (const VirtualExtruder& definition : other_container->virtual_extruders()) {
            input.reserved_virtual_ids.insert(definition.id);
        }

        if (other_container->print_technology() == PrinterTechnology::FFF) {
            input.max_physical_slot_count = std::max(
                input.max_physical_slot_count,
                static_cast<unsigned int>(
                    other_container->selected_preset().hw_config.material_slot_count()
                )
            );
        }
    }

    return input;
}

} // namespace

ColorMixDialog::ColorMixDialog(
    ProjectInteractor& project_interactor,
    Navigator& navigator,
    LogicalPrinterSettingsDialog* logical_printer_settings_dialog
) :
    Dialog({_u8L("Color Mixing")}, "ColorMixDialog"),
    m_colors_changed_listener_scope(project_interactor.project_settings_interactor(), *this),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_selected_config_container_changed_listener_scope(project_interactor, *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_logical_printer_settings_dialog(logical_printer_settings_dialog)
{
    this->content_item()->set_width(980_fpx);
    this->content_item()->set_height(660_fpx);

    this->content()->set_orientation(Orientation::Vertical);
    this->content()->set_padding(0);
    this->content()->set_gap(0);
    this->content()->set_flex_grow(1.f);

    m_few_extruders_hint = this->content()->emplace_back<Text>(_u8L(
        "Virtual extruders need at least two physical extruders. "
        "Increase the extruder count in Printer Settings first."
    ));
    m_few_extruders_hint->set_wrap_mode(Text::WrapMode::Wrap);
    m_few_extruders_hint->set_margin({16_fpx, 12_fpx, 16_fpx, 0});
    m_few_extruders_hint->set_visible(false);

    Item* main_row = this->content()->emplace_back<Item>();
    main_row->set_orientation(Orientation::Horizontal);
    main_row->set_flex_grow(1.f);

    this->create_left_panel(main_row);
    main_row->emplace_back<Separator>(Orientation::Vertical);
    this->create_right_panel(main_row);
    this->create_footer(this->content());
}

void ColorMixDialog::on_about_to_show()
{
    this->update_from_project();
}

void ColorMixDialog::on_colors_changed(
    SelectionId project_id,
    SelectionId config_container_id,
    const std::vector<ColorRGB>& colors
)
{
    this->update_physical_colors();
}

void ColorMixDialog::on_preset_selection_changed(
    SelectionId project_id,
    SelectionId config_container_id,
    PresetItemType type
)
{
    this->update_physical_colors();
}

void ColorMixDialog::
    on_selected_config_container_changed(SelectionId project_id, SelectionId config_container_id)
{
    if (this->opened()) {
        this->update_from_project();
    }
}

void ColorMixDialog::update_physical_colors()
{
    if (!this->opened()) {
        return;
    }

    const ColorMixDialogInput input = collect_color_mix_dialog_input(m_project_interactor);
    if (input.physical_extruder_count != m_physical_extruder_count) {
        this->update_from_project();
        return;
    }

    m_physical_colors.clear();
    m_physical_colors.reserve(m_physical_extruder_count);
    for (unsigned int i = 0; i < m_physical_extruder_count; ++i) {
        std::string color =
            i < input.physical_colors.size() ? input.physical_colors.at(i) : std::string{};
        if (color.empty()) {
            color = FALLBACK_COLOR;
        }

        m_physical_colors.push_back(std::move(color));
    }

    m_physical_types.clear();
    m_physical_types.reserve(m_physical_extruder_count);
    for (unsigned int i = 0; i < m_physical_extruder_count; ++i) {
        m_physical_types.push_back(
            i < input.physical_types.size() ? input.physical_types.at(i) : std::string{}
        );
    }

    for (size_t i = 0; i < m_recipe_rows.size() && i < m_working_virtual_extruders.size(); ++i) {
        m_recipe_rows.at(i)->update(
            ColorMixRecipeRow::make_row_data(m_working_virtual_extruders.at(i), m_physical_colors)
        );
    }

    this->rebuild_filter_buttons();

    if (this->selected_index() >= 0) {
        this->update_preview_and_validation();
    } else {
        this->rebuild_preset_grid();
    }
}

void ColorMixDialog::create_left_panel(Item* parent)
{
    Item* left_panel = parent->emplace_back<Item>();
    left_panel->set_orientation(Orientation::Vertical);
    left_panel->set_width(LEFT_PANEL_WIDTH);
    left_panel->set_flex_shrink(0);

    const auto deselect_action = [this]()
    {
        if (m_selected_index >= 0) {
            this->select_row(-1);
        }
    };
    const ImColor transparent_color = m_theme->color_imgui(Color::Transparent);

    RectangleButton* deselect_catcher = left_panel->emplace_back<RectangleButton>();
    deselect_catcher->set_position_type(YGPositionTypeAbsolute);
    deselect_catcher->set_left(0);
    deselect_catcher->set_top(0);
    deselect_catcher->set_width_percent(100);
    deselect_catcher->set_height_percent(100);
    deselect_catcher->set_background_color(transparent_color, transparent_color);
    deselect_catcher->set_allow_overlap(true);
    deselect_catcher->callbacks().action = deselect_action;

    Text* panel_title =
        left_panel->emplace_back<Text>(_u8L("Virtual Extruders"), Render::ImguiFontType::Bold);
    panel_title->set_margin({16_fpx, 14_fpx, 16_fpx, 10_fpx});
    panel_title->set_flex_shrink(0);

    ScrollArea* list_scroll = left_panel->emplace_back<ScrollArea>();
    list_scroll->set_flex_grow(1.f);
    list_scroll->set_min_height(0);
    list_scroll->set_orientation(Orientation::Vertical);
    list_scroll->set_padding({8_fpx, 0, 18_fpx, 0});

    m_list_container = list_scroll->emplace_back<Item>();
    m_list_container->set_orientation(Orientation::Vertical);
    m_list_container->set_width_percent(100);
    m_list_container->set_gap(2_fpx);
    m_list_container->set_flex_shrink(0);

    RectangleButton* list_empty_space = list_scroll->emplace_back<RectangleButton>();
    list_empty_space->set_flex_grow(1.f);
    list_empty_space->set_background_color(transparent_color, transparent_color);
    list_empty_space->callbacks().action = deselect_action;

    left_panel->emplace_back<Separator>(Orientation::Horizontal);

    Item* buttons_row = left_panel->emplace_back<Item>();
    buttons_row->set_orientation(Orientation::Horizontal);
    buttons_row->set_gap(PANEL_GAP);
    buttons_row->set_padding(12_fpx);
    buttons_row->set_flex_shrink(0);

    m_add_blend_button = buttons_row->emplace_back<LayoutButton>(
        _u8L("Add blend"),
        Render::Icon::Plus,
        _u8L("Create a blend recipe that alternates two filaments layer by layer.")
    );
    m_add_blend_button->set_content_padding({10_fpx, 5_fpx});
    m_add_blend_button->callbacks().action = [this]() { this->on_add_blend_clicked(); };
}

void ColorMixDialog::create_right_panel(Item* parent)
{
    Item* right_panel = parent->emplace_back<Item>();
    right_panel->set_orientation(Orientation::Vertical);
    right_panel->set_flex_grow(1.f);
    right_panel->set_padding(20_fpx);

    m_right_stack = right_panel->emplace_back<StackLayout>();
    m_right_stack->set_flex_grow(1.f);

    m_presets_pane = m_right_stack->emplace_back<Item>();
    m_presets_pane->set_flex_grow(1.f);
    this->create_presets_pane(m_presets_pane);

    m_editor_pane = m_right_stack->emplace_back<Item>();
    m_editor_pane->set_flex_grow(1.f);
    this->create_editor_pane(m_editor_pane);

    m_right_stack->set_current_index(0);
}

void ColorMixDialog::create_presets_pane(Item* pane)
{
    pane->set_orientation(Orientation::Vertical);
    pane->set_gap(PANEL_GAP);

    const ImColor muted_text_color = m_theme->color_imgui(Color::Text, ColorGroup::Disabled);

    Text* intro_text = pane->emplace_back<Text>(_u8L(
        "A virtual extruder blends physical filaments layer by layer to create "
        "new colors. Click a blend below to add it to your project."
    ));
    intro_text->set_wrap_mode(Text::WrapMode::Wrap);
    intro_text->set_text_color(muted_text_color);

    Text* filter_label =
        pane->emplace_back<Text>(_u8L("Toggle extruders to filter the blends shown below"));
    filter_label->set_text_color(muted_text_color);

    m_filter_button_row = pane->emplace_back<Item>();
    m_filter_button_row->set_orientation(Orientation::Horizontal);
    m_filter_button_row->set_flex_wrap(YGWrapWrap);
    m_filter_button_row->set_gap(4_fpx);

    ScrollArea* presets_scroll = pane->emplace_back<ScrollArea>();
    presets_scroll->set_flex_grow(1.f);
    presets_scroll->set_min_height(0);
    presets_scroll->set_orientation(Orientation::Vertical);

    Item* presets_content = presets_scroll->emplace_back<Item>();
    presets_content->set_orientation(Orientation::Vertical);
    presets_content->set_width_percent(100);
    presets_content->set_gap(PANEL_GAP);
    presets_content->set_flex_shrink(0);

    m_two_color_presets_title =
        presets_content->emplace_back<Text>(_u8L("Two-color blends"), Render::ImguiFontType::Bold);
    m_two_color_presets_wrap = presets_content->emplace_back<Item>();
    m_two_color_presets_wrap->set_orientation(Orientation::Horizontal);
    m_two_color_presets_wrap->set_flex_wrap(YGWrapWrap);
    m_two_color_presets_wrap->set_gap(PANEL_GAP);

    m_three_color_presets_title = presets_content->emplace_back<
        Text>(_u8L("Three-color blends"), Render::ImguiFontType::Bold);
    m_three_color_presets_wrap = presets_content->emplace_back<Item>();
    m_three_color_presets_wrap->set_orientation(Orientation::Horizontal);
    m_three_color_presets_wrap->set_flex_wrap(YGWrapWrap);
    m_three_color_presets_wrap->set_gap(PANEL_GAP);

    m_presets_empty_hint = presets_content->emplace_back<Text>(_u8L(
        "No combinations available. Presets pair up filaments of the same "
        "type (e.g. PLA + PLA); adjust filament types in the printer preset "
        "to see more suggestions."
    ));
    m_presets_empty_hint->set_wrap_mode(Text::WrapMode::Wrap);
    m_presets_empty_hint->set_text_color(muted_text_color);
    m_presets_empty_hint->set_visible(false);
}

void ColorMixDialog::create_editor_pane(Item* pane)
{
    pane->set_orientation(Orientation::Vertical);

    ScrollArea* editor_scroll = pane->emplace_back<ScrollArea>();
    editor_scroll->set_flex_grow(1.f);
    editor_scroll->set_min_height(0);
    editor_scroll->set_orientation(Orientation::Vertical);
    editor_scroll->set_padding({0, 0, 18_fpx, 0});

    Item* editor_content = editor_scroll->emplace_back<Item>();
    editor_content->set_orientation(Orientation::Vertical);
    editor_content->set_width_percent(100);
    editor_content->set_gap(PANEL_GAP);
    editor_content->set_flex_shrink(0);

    m_editor_title =
        editor_content->emplace_back<Text>(_u8L("Virtual Extruder"), Render::ImguiFontType::Bold);

    m_editor_description = editor_content->emplace_back<Text>(std::string{});
    m_editor_description->set_wrap_mode(Text::WrapMode::Wrap);
    m_editor_description->set_text_color(m_theme->color_imgui(Color::Text, ColorGroup::Disabled));

    m_editor_rows = editor_content->emplace_back<Item>();
    m_editor_rows->set_orientation(Orientation::Vertical);
    m_editor_rows->set_gap(6_fpx);

    m_add_component_button = editor_content->emplace_back<LayoutButton>(_u8L("Add component"));
    m_add_component_button->set_content_padding({10_fpx, 5_fpx});
    m_add_component_button->set_self_align(YGAlignFlexStart);
    m_add_component_button->callbacks().action = [this]()
    { this->on_add_or_remove_component_clicked(); };

    Item* color_block = editor_content->emplace_back<Item>();
    color_block->set_orientation(Orientation::Vertical);
    color_block->set_gap(6_fpx);

    Text* color_label = color_block->emplace_back<Text>(_u8L("Display color"));
    color_label->set_text_color(m_theme->color_imgui(Color::Text, ColorGroup::Disabled));

    Item* color_row = color_block->emplace_back<Item>();
    color_row->set_orientation(Orientation::Horizontal);
    color_row->set_align_items(YGAlignCenter);
    color_row->set_gap(PANEL_GAP);

    m_color_picker_button = color_row->emplace_back<ColorPickerButton>();
    m_color_picker_button->set_width(100_fpx);
    m_color_picker_button->set_height(28_fpx);
    m_color_picker_button->set_background_color_border(
        m_theme->color_imgui(Color::Text, ColorGroup::Disabled),
        m_theme->color_imgui(Color::Text, ColorGroup::Disabled)
    );
    m_color_picker_button->set_tooltip(_u8L(
        "Preview of the blended color. Click to pick a custom display color. "
        "Resets automatically when you change the composition."
    ));
    m_color_picker_button->callbacks().color_edited = [this](const ImColor& color)
    {
        const int selection = this->selected_index();
        if (selection < 0) {
            return;
        }

        m_working_virtual_extruders.at(selection).color =
            Algorithms::Color::encode_color(ColorRGB(color.Value.x, color.Value.y, color.Value.z));
        this->update_preview_and_validation();
    };

    m_reset_display_color_button = color_row->emplace_back<LayoutButton>(_u8L("Reset to blended"));
    m_reset_display_color_button->set_content_padding({8_fpx, 4_fpx});
    m_reset_display_color_button->set_background_color(Color::ButtonTransparent);
    m_reset_display_color_button->set_visible(false);
    m_reset_display_color_button->callbacks().action = [this]()
    {
        this->clear_color_override();
        this->update_preview_and_validation();
    };

    Text* sequence_title = editor_content->emplace_back<Text>(_u8L("Layer sequence preview"));
    sequence_title->set_text_color(m_theme->color_imgui(Color::Text, ColorGroup::Disabled));

    m_sequence_bar = editor_content->emplace_back<LayerSequenceBar>();
    m_sequence_bar->set_tooltip(
        _u8L("Each cell is one print layer. The pattern shows how filaments alternate.")
    );
}

void ColorMixDialog::create_footer(Item* parent)
{
    parent->emplace_back<Separator>(Orientation::Horizontal);

    Item* footer = parent->emplace_back<Item>();
    footer->set_orientation(Orientation::Horizontal);
    footer->set_justify_content(YGJustifyFlexEnd);
    footer->set_gap(10_fpx);
    footer->set_padding({16_fpx, 12_fpx});
    footer->set_flex_shrink(0);

    const Paddings button_padding{15_fpx, 5_fpx};

    LayoutButton* cancel_button = footer->emplace_back<LayoutButton>(_u8L("Cancel"));
    cancel_button->set_content_padding(button_padding);
    cancel_button->set_background_color(Color::ButtonTransparent);
    cancel_button->callbacks().action = [this]() { this->close_action(); };

    LayoutButton* ok_button = footer->emplace_back<LayoutButton>(_u8L("OK"));
    ok_button->set_content_padding(button_padding);
    ok_button->set_background_color(Color::AccentPrimary);
    ok_button->set_label_color(ImColor(255, 255, 255));
    ok_button->callbacks().action = [this]() { this->on_accept_clicked(); };
}

void ColorMixDialog::update_from_project()
{
    ColorMixDialogInput input = collect_color_mix_dialog_input(m_project_interactor);

    m_target_project_id          = m_project_interactor.selected_project_id();
    m_target_config_container_id = m_project_interactor.selected_config_container_id();

    m_physical_extruder_count = input.physical_extruder_count;

    m_physical_colors.clear();
    m_physical_colors.reserve(m_physical_extruder_count);
    for (unsigned int i = 0; i < m_physical_extruder_count; ++i) {
        std::string color =
            i < input.physical_colors.size() ? input.physical_colors.at(i) : std::string{};
        if (color.empty()) {
            color = FALLBACK_COLOR;
        }

        m_physical_colors.push_back(std::move(color));
    }

    m_physical_types.clear();
    m_physical_types.reserve(m_physical_extruder_count);
    for (unsigned int i = 0; i < m_physical_extruder_count; ++i) {
        m_physical_types.push_back(
            i < input.physical_types.size() ? input.physical_types.at(i) : std::string{}
        );
    }

    m_object_use_counts         = std::move(input.object_use_counts);
    m_working_virtual_extruders = std::move(input.virtual_extruders);
    m_reserved_virtual_ids      = std::move(input.reserved_virtual_ids);
    m_max_physical_slot_count   = input.max_physical_slot_count;

    m_original_ids.clear();
    m_original_ids.reserve(m_working_virtual_extruders.size());
    for (const VirtualExtruder& virtual_extruder : std::as_const(m_working_virtual_extruders)) {
        m_original_ids.push_back(virtual_extruder.id);
    }

    m_removed_ids.clear();

    m_preset_extruders_enabled.assign(m_physical_extruder_count, true);
    m_selected_index = -1;

    m_few_extruders_hint->set_visible(m_physical_extruder_count < 2);
    m_add_blend_button->set_enabled(m_physical_extruder_count >= 2);

    this->rebuild_filter_buttons();
    this->rebuild_virtual_extruder_list(-1);
}

void ColorMixDialog::close_action()
{
    m_navigator.set_opened_dialog(m_logical_printer_settings_dialog);
}

void ColorMixDialog::on_accept_clicked()
{
    const tl::expected<void, std::string> apply_result =
        m_project_interactor.apply_virtual_extruders(
            m_target_project_id,
            m_target_config_container_id,
            m_working_virtual_extruders,
            m_removed_ids
        );
    if (!apply_result.has_value()) {
        AppServices::instance().dialog_manager().show_error_dialog(apply_result.error());
        SPDLOG_ERROR("ColorMix apply failed: {}", apply_result.error());
        return;
    }

    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::EditVirtualExtruders);
    this->close_action();
}

void ColorMixDialog::rebuild_virtual_extruder_list(int select_index)
{
    clear_children_later(m_list_container);
    m_recipe_rows.clear();

    for (size_t i = 0; i < m_working_virtual_extruders.size(); ++i) {
        ColorMixRecipeRow* row = m_list_container->emplace_back<ColorMixRecipeRow>(
            ColorMixRecipeRow::make_row_data(m_working_virtual_extruders.at(i), m_physical_colors)
        );
        const int row_index             = static_cast<int>(i);
        row->callbacks().selected       = [this, row_index]() { this->on_row_clicked(row_index); };
        row->callbacks().remove_clicked = [this, row_index]()
        { this->on_row_remove_clicked(row_index); };
        m_recipe_rows.push_back(row);
    }

    if (select_index >= 0 && select_index < static_cast<int>(m_working_virtual_extruders.size())) {
        this->select_row(select_index);
    } else {
        this->select_row(-1);
    }
}

void ColorMixDialog::select_row(int index)
{
    m_selected_index = index;
    for (size_t i = 0; i < m_recipe_rows.size(); ++i) {
        m_recipe_rows.at(i)->set_selected(static_cast<int>(i) == index);
    }

    this->update_right_panel_visibility();
    if (index >= 0) {
        this->populate_editor_from_selection();
    }
}

void ColorMixDialog::on_row_clicked(int index)
{
    if (index >= 0
        && index < static_cast<int>(m_working_virtual_extruders.size())
        && m_working_virtual_extruders.at(index).type() == VirtualExtruder::Type::Gradient)
    {
        // Gradient recipes are only carried over from the project, they have no editor.
        return;
    }

    this->select_row(index == m_selected_index ? -1 : index);
}

void ColorMixDialog::on_row_remove_clicked(int index)
{
    if (index < 0 || index >= static_cast<int>(m_working_virtual_extruders.size())) {
        return;
    }

    const unsigned int removed_id = m_working_virtual_extruders.at(index).id;

    const auto do_remove = [this, index, removed_id]()
    {
        if (index >= static_cast<int>(m_working_virtual_extruders.size())
            || m_working_virtual_extruders.at(index).id != removed_id)
        {
            return;
        }

        m_working_virtual_extruders.erase(m_working_virtual_extruders.begin() + index);
        if (std::ranges::find(m_original_ids, removed_id) != m_original_ids.end()) {
            m_removed_ids.push_back(removed_id);
        }

        this->rebuild_virtual_extruder_list(-1);
    };

    const int users = this->count_objects_using(removed_id);
    if (users > 0) {
        AppServices::instance().dialog_manager().show_yesno_dialog(
            _u8L("Remove virtual extruder"),
            fmt::format(
                fmt::runtime(_u8L(
                    "Virtual extruder {} is assigned to {} object(s) or modifier(s). "
                    "Removing it will reset their extruder to default. Continue?"
                )),
                removed_id,
                users
            ),
            [do_remove](bool answer)
            {
                if (answer) {
                    do_remove();
                }
            }
        );
    } else {
        do_remove();
    }
}

void ColorMixDialog::update_right_panel_visibility()
{
    const bool has_selection = this->selected_index() >= 0;
    if (!has_selection) {
        this->rebuild_preset_grid();
    }

    m_right_stack->set_current_index(has_selection ? 1 : 0);
}

void ColorMixDialog::populate_editor_from_selection()
{
    const int selection = this->selected_index();
    if (selection < 0) {
        return;
    }

    m_editor_title->set_text(recipe_title(m_working_virtual_extruders.at(selection)));
    this->rebuild_component_rows();
    this->update_preview_and_validation();
}

void ColorMixDialog::clear_editor_rows()
{
    clear_children_later(m_editor_rows);
    m_component_dropdowns.clear();
    m_component_percent_texts.clear();
    m_inline_ratio_bar = nullptr;
    m_inline_triangle  = nullptr;
}

Yoga::ColorDropdown* ColorMixDialog::create_extruder_dropdown(
    Item* parent,
    unsigned int extruder_id_1based,
    const std::function<void(unsigned int extruder_id_1based)>& on_selected
)
{
    if (extruder_id_1based == 0 || extruder_id_1based > m_physical_extruder_count) {
        extruder_id_1based = 1;
    }

    // A blend is always made of physical filaments, so the virtual extruders are left out.
    ColorDropdown* dropdown =
        parent->emplace_back<ColorDropdown>(m_project_interactor, false, true, false);
    dropdown->set_width(EXTRUDER_DROPDOWN_WIDTH);
    dropdown->set_flex_shrink(0);
    dropdown->set_current_index(
        dropdown->index_of_extruder_id(static_cast<int>(extruder_id_1based))
    );
    dropdown->on_color_selected = [dropdown, on_selected](size_t index)
    { on_selected(static_cast<unsigned int>(dropdown->extruder_id_at(index))); };
    return dropdown;
}

void ColorMixDialog::rebuild_component_rows()
{
    this->clear_editor_rows();

    const int selection = this->selected_index();
    if (selection < 0) {
        return;
    }

    const VirtualExtruder& virtual_extruder = m_working_virtual_extruders.at(selection);
    const size_t component_count            = virtual_extruder.components.size();
    const bool is_two_component             = component_count == 2;
    const bool is_three_component           = component_count == 3;

    const auto component_extruder_changed = [this](size_t component_index)
    {
        return [this, component_index](unsigned int extruder_id_1based)
        {
            const int selection = this->selected_index();
            if (selection < 0
                || component_index >= m_working_virtual_extruders.at(selection).components.size())
            {
                return;
            }

            m_working_virtual_extruders.at(selection).components.at(component_index).extruder_id =
                extruder_id_1based;
            this->on_composition_changed();
        };
    };

    const auto make_percent_text = [](Item* parent) -> Text*
    {
        Text* percent_text = parent->emplace_back<Text>(std::string{}, Render::ImguiFontType::Bold);
        percent_text->set_font_size(PERCENT_FONT_SIZE);
        percent_text->set_text_color(m_theme->color_imgui(Color::Text));
        percent_text->set_min_width(60_fpx);
        return percent_text;
    };

    if (is_two_component) {
        Item* dropdown_row = m_editor_rows->emplace_back<Item>();
        dropdown_row->set_orientation(Orientation::Horizontal);
        dropdown_row->set_justify_content(YGJustifySpaceBetween);
        dropdown_row->set_margin({0, 0, 0, 4_fpx});

        ColorDropdown* dropdown_a = this->create_extruder_dropdown(
            dropdown_row,
            virtual_extruder.components.at(0).extruder_id,
            component_extruder_changed(0)
        );
        dropdown_a->set_width(200_fpx);

        ColorDropdown* dropdown_b = this->create_extruder_dropdown(
            dropdown_row,
            virtual_extruder.components.at(1).extruder_id,
            component_extruder_changed(1)
        );
        dropdown_b->set_width(200_fpx);

        m_inline_ratio_bar = m_editor_rows->emplace_back<BlendRatioBar>();
        m_inline_ratio_bar->set_width_percent(100);
        m_inline_ratio_bar->set_tooltip(_u8L(
            "Drag the handle to adjust the mix ratio. "
            "More % = more layers of that filament per cycle."
        ));
        m_inline_ratio_bar->callbacks().ratio_changed = [this](int)
        { this->on_composition_changed(); };
        m_inline_ratio_bar->set_ratio(
            static_cast<int>(std::round(virtual_extruder.components.at(0).ratio * 100.0))
        );

        m_component_dropdowns.push_back(dropdown_a);
        m_component_dropdowns.push_back(dropdown_b);

        Item* percent_row = m_editor_rows->emplace_back<Item>();
        percent_row->set_orientation(Orientation::Horizontal);
        percent_row->set_justify_content(YGJustifySpaceBetween);

        m_component_percent_texts.push_back(make_percent_text(percent_row));
        Text* percent_b = make_percent_text(percent_row);
        percent_b->set_align({AlignH::Right, AlignV::Top});
        m_component_percent_texts.push_back(percent_b);
    } else if (is_three_component) {
        Item* mixer_column = m_editor_rows->emplace_back<Item>();
        mixer_column->set_orientation(Orientation::Vertical);
        mixer_column->set_align_items(YGAlignCenter);
        mixer_column->set_self_align(YGAlignCenter);
        mixer_column->set_width(380_fpx);
        mixer_column->set_gap(6_fpx);

        Text* percent_0 = make_percent_text(mixer_column);
        percent_0->set_align({AlignH::Center, AlignV::Top});
        m_component_percent_texts.push_back(percent_0);

        ColorDropdown* dropdown_0 = this->create_extruder_dropdown(
            mixer_column,
            virtual_extruder.components.at(0).extruder_id,
            component_extruder_changed(0)
        );
        dropdown_0->set_width(220_fpx);

        m_inline_triangle = mixer_column->emplace_back<BarycentricRatioPicker>();
        m_inline_triangle->set_width(256_fpx);
        m_inline_triangle->set_height(200_fpx);
        m_inline_triangle->set_tooltip(_u8L(
            "Drag the handle to set the share of each filament. "
            "Closer to a corner = more of that filament."
        ));
        m_inline_triangle->callbacks().weights_changed = [this]()
        { this->on_composition_changed(); };
        m_inline_triangle->set_weights(
            virtual_extruder.components.at(0).ratio,
            virtual_extruder.components.at(1).ratio,
            virtual_extruder.components.at(2).ratio
        );

        Item* bottom_row = mixer_column->emplace_back<Item>();
        bottom_row->set_orientation(Orientation::Horizontal);
        bottom_row->set_width_percent(100);
        bottom_row->set_gap(12_fpx);

        const auto make_bottom_column = [&](size_t component_index) -> ColorDropdown*
        {
            Item* column = bottom_row->emplace_back<Item>();
            column->set_orientation(Orientation::Vertical);
            column->set_align_items(YGAlignCenter);
            column->set_flex_grow(1.f);
            column->set_gap(4_fpx);

            ColorDropdown* dropdown = this->create_extruder_dropdown(
                column,
                virtual_extruder.components.at(component_index).extruder_id,
                component_extruder_changed(component_index)
            );

            Text* percent_text = make_percent_text(column);
            percent_text->set_align({AlignH::Center, AlignV::Top});
            m_component_percent_texts.push_back(percent_text);
            return dropdown;
        };

        ColorDropdown* dropdown_1 = make_bottom_column(1);
        ColorDropdown* dropdown_2 = make_bottom_column(2);

        m_component_dropdowns.push_back(dropdown_0);
        m_component_dropdowns.push_back(dropdown_1);
        m_component_dropdowns.push_back(dropdown_2);
    }

    if (is_two_component) {
        m_add_component_button->set_label(_u8L("Add third extruder"));
        m_add_component_button->set_icon(Render::Icon::Plus);
        m_add_component_button->set_tooltip(
            _u8L("Add a third filament to create a three-way blend with the triangle mixer.")
        );
        m_add_component_button->set_enabled(m_physical_extruder_count >= 3);
        m_editor_description->set_text(_u8L(
            "Drag the slider to set how many layers each filament gets in the repeating pattern."
        ));
    } else if (is_three_component) {
        m_add_component_button->set_label(_u8L("Remove third extruder"));
        m_add_component_button->set_icon(Render::Icon::Minus);
        m_add_component_button->set_tooltip(
            _u8L("Remove the third filament and go back to the two-way slider mixer.")
        );
        m_add_component_button->set_enabled(true);
        m_editor_description->set_text(
            _u8L("Drag the handle inside the triangle to set the share of each filament.")
        );
    }
}

void ColorMixDialog::update_preview_and_validation()
{
    const int selection = this->selected_index();
    if (selection < 0) {
        return;
    }

    VirtualExtruder& virtual_extruder = m_working_virtual_extruders.at(selection);
    const size_t component_count      = virtual_extruder.components.size();

    for (size_t i = 0; i < component_count && i < m_component_dropdowns.size(); ++i) {
        virtual_extruder.components.at(i)
            .extruder_id = static_cast<unsigned int>(m_component_dropdowns.at(i)->extruder_id_at(
            m_component_dropdowns.at(i)->current_index()
        ));
    }

    if (component_count == 2 && m_inline_ratio_bar != nullptr) {
        const int ratio_a_percent               = m_inline_ratio_bar->ratio();
        virtual_extruder.components.at(0).ratio = static_cast<double>(ratio_a_percent) / 100.0;
        virtual_extruder.components.at(1).ratio =
            1.0 - static_cast<double>(ratio_a_percent) / 100.0;

        m_inline_ratio_bar->set_colors(
            this->physical_color(virtual_extruder.components.at(0).extruder_id),
            this->physical_color(virtual_extruder.components.at(1).extruder_id)
        );
    }

    if (component_count == 3 && m_inline_triangle != nullptr) {
        const double weight_0                   = m_inline_triangle->weight_0();
        const double weight_1                   = m_inline_triangle->weight_1();
        const double weight_2                   = std::max(0.0, 1.0 - weight_0 - weight_1);
        virtual_extruder.components.at(0).ratio = weight_0;
        virtual_extruder.components.at(1).ratio = weight_1;
        virtual_extruder.components.at(2).ratio = weight_2;

        m_inline_triangle->set_colors(
            this->physical_color(virtual_extruder.components.at(0).extruder_id),
            this->physical_color(virtual_extruder.components.at(1).extruder_id),
            this->physical_color(virtual_extruder.components.at(2).extruder_id)
        );
        m_inline_triangle->set_vertex_labels(
            std::to_string(virtual_extruder.components.at(0).extruder_id),
            std::to_string(virtual_extruder.components.at(1).extruder_id),
            std::to_string(virtual_extruder.components.at(2).extruder_id)
        );
    }

    double ratio_sum = 0.0;
    for (const VirtualExtruderComponent& component : virtual_extruder.components) {
        ratio_sum += component.ratio;
    }

    std::vector<ImColor> cycle_colors;
    if (ratio_sum > 1e-9) {
        const std::vector<unsigned int> sequence =
            Algorithms::VirtualExtruder::build_sequence(virtual_extruder);
        cycle_colors.reserve(sequence.size());
        for (const unsigned int slot : sequence) {
            cycle_colors.push_back(this->physical_color(slot));
        }
    }

    for (size_t i = 0; i < component_count && i < m_component_percent_texts.size(); ++i) {
        const int percent =
            static_cast<int>(std::round(virtual_extruder.components.at(i).ratio * 100.0));
        m_component_percent_texts.at(i)->set_text(fmt::format("{}%", percent));
    }

    m_sequence_bar->set_cycle(std::move(cycle_colors));

    this->update_selected_list_row();

    m_editor_title->set_text(recipe_title(virtual_extruder));

    const ImColor display_color = virtual_extruder.color.has_value() ?
        parse_hex_color(*virtual_extruder.color) :
        parse_hex_color(effective_color_hex(virtual_extruder, m_physical_colors));
    m_color_picker_button->set_color(display_color);
    m_color_picker_button->set_background_color(display_color, display_color);
    m_reset_display_color_button->set_visible(virtual_extruder.color.has_value());
}

void ColorMixDialog::update_selected_list_row()
{
    const int selection = this->selected_index();
    if (selection < 0 || selection >= static_cast<int>(m_recipe_rows.size())) {
        return;
    }

    m_recipe_rows.at(selection)->update(
        ColorMixRecipeRow::
            make_row_data(m_working_virtual_extruders.at(selection), m_physical_colors)
    );
}

void ColorMixDialog::on_add_blend_clicked()
{
    if (m_physical_extruder_count < 2) {
        return;
    }

    VirtualExtruder virtual_extruder;
    virtual_extruder.id = this->next_free_virtual_id();
    virtual_extruder.components.push_back({1u, 0.5});
    virtual_extruder.components.push_back({2u, 0.5});
    m_working_virtual_extruders.push_back(std::move(virtual_extruder));
    this->rebuild_virtual_extruder_list(static_cast<int>(m_working_virtual_extruders.size()) - 1);
}

void ColorMixDialog::on_composition_changed()
{
    this->clear_color_override();
    this->update_preview_and_validation();
}

void ColorMixDialog::on_add_or_remove_component_clicked()
{
    const int selection = this->selected_index();
    if (selection < 0) {
        return;
    }

    VirtualExtruder& virtual_extruder = m_working_virtual_extruders.at(selection);
    if (virtual_extruder.components.size() == 3) {
        this->on_remove_component(2);
        return;
    }

    const size_t component_cap =
        std::min<size_t>(Domain::MAX_BLEND_COMPONENTS, m_physical_extruder_count);
    if (virtual_extruder.components.size() >= component_cap) {
        return;
    }

    const unsigned int next_id = this->pick_unused_physical_id(virtual_extruder);

    // Whole percent so that the editor never shows a split adding up to 99.
    const std::vector<int> balanced = Algorithms::VirtualExtruder::balanced_ratios_percent(
        virtual_extruder.components.size() + 1
    );
    for (size_t i = 0; i < virtual_extruder.components.size(); ++i) {
        virtual_extruder.components.at(i).ratio = balanced.at(i) / 100.0;
    }

    virtual_extruder.components.push_back({next_id, balanced.back() / 100.0});
    virtual_extruder.color.reset();

    this->rebuild_component_rows();
    this->update_preview_and_validation();
}

void ColorMixDialog::on_remove_component(size_t component_index)
{
    const int selection = this->selected_index();
    if (selection < 0) {
        return;
    }

    VirtualExtruder& virtual_extruder = m_working_virtual_extruders.at(selection);
    if (virtual_extruder.components.size() <= 2
        || component_index >= virtual_extruder.components.size())
    {
        return;
    }

    virtual_extruder.components.erase(
        virtual_extruder.components.begin() + static_cast<int>(component_index)
    );
    double ratio_sum = 0.0;
    for (const VirtualExtruderComponent& component : virtual_extruder.components) {
        ratio_sum += component.ratio;
    }

    if (ratio_sum > 0.0) {
        for (VirtualExtruderComponent& component : virtual_extruder.components) {
            component.ratio /= ratio_sum;
        }
    }

    virtual_extruder.color.reset();

    this->rebuild_component_rows();
    this->update_preview_and_validation();
}

void ColorMixDialog::rebuild_filter_buttons()
{
    clear_children_later(m_filter_button_row);

    for (unsigned int i = 0; i < m_physical_extruder_count; ++i) {
        ExtruderFilterButton* filter_button = m_filter_button_row->emplace_back<
            ExtruderFilterButton>(i + 1, this->physical_color(i + 1));
        if (i < m_preset_extruders_enabled.size()) {
            filter_button->set_checked(m_preset_extruders_enabled.at(i));
        }

        filter_button->set_tooltip(
            fmt::format(fmt::runtime(_u8L("Toggle extruder {} for preset generation")), i + 1)
        );

        const unsigned int extruder_index          = i;
        filter_button->callbacks().checked_changed = [this, extruder_index](bool checked)
        {
            if (extruder_index < m_preset_extruders_enabled.size()) {
                m_preset_extruders_enabled.at(extruder_index) = checked;
                this->rebuild_preset_grid();
            }
        };
    }
}

PhysicalExtruderSlots ColorMixDialog::preset_slots() const
{
    PhysicalExtruderSlots slots;
    slots.reserve(m_physical_extruder_count);
    for (unsigned int i = 0; i < m_physical_extruder_count; ++i) {
        slots.push_back(
            {i < m_physical_colors.size() ? m_physical_colors.at(i) : std::string{},
             i < m_physical_types.size() ? m_physical_types.at(i) : std::string{},
             i >= m_preset_extruders_enabled.size() || m_preset_extruders_enabled.at(i)}
        );
    }

    return slots;
}

void ColorMixDialog::rebuild_preset_grid()
{
    clear_children_later(m_two_color_presets_wrap);
    clear_children_later(m_three_color_presets_wrap);

    const PhysicalExtruderSlots slots = this->preset_slots();
    const BlendPresetGroups groups =
        Biz::Algorithms::VirtualExtruderPresets::build_blend_presets(slots);

    const auto add_preset_button = [this, &slots](Item* wrap, const BlendPreset& preset)
    {
        std::string label;
        std::string tooltip;
        for (size_t i = 0; i < preset.extruder_ids_1based.size(); ++i) {
            if (i > 0) {
                label += '+';
                tooltip += " + ";
            }

            label += std::to_string(preset.extruder_ids_1based.at(i));
            tooltip += fmt::format(
                fmt::runtime(_u8L("Ext {} ({} %)")),
                preset.extruder_ids_1based.at(i),
                preset.ratios_percent.at(i)
            );
        }

        const bool used         = this->find_matching_blend_index(preset) >= 0;
        BlendRecipeTile* button = wrap->emplace_back<BlendRecipeTile>(
            parse_hex_color(Biz::Algorithms::VirtualExtruderPresets::preset_color(preset, slots)),
            label,
            used
        );
        button->set_tooltip(
            used ? _u8L("Already added - click to remove") : _u8L("Click to add: ") + tooltip
        );
        button->callbacks().action = [this, preset]() { this->on_preset_clicked(preset); };
    };

    for (const BlendPreset& preset : groups.two_color) {
        add_preset_button(m_two_color_presets_wrap, preset);
    }

    for (const BlendPreset& preset : groups.three_color) {
        add_preset_button(m_three_color_presets_wrap, preset);
    }

    const bool has_two   = !groups.two_color.empty();
    const bool has_three = !groups.three_color.empty();
    m_two_color_presets_title->set_visible(has_two);
    m_two_color_presets_wrap->set_visible(has_two);
    m_three_color_presets_title->set_visible(has_three);
    m_three_color_presets_wrap->set_visible(has_three);
    m_presets_empty_hint->set_visible(!has_two && !has_three);
}

void ColorMixDialog::on_preset_clicked(const BlendPreset& preset)
{
    const int existing_index = this->find_matching_blend_index(preset);
    if (existing_index >= 0) {
        this->on_row_remove_clicked(existing_index);
        return;
    }

    VirtualExtruder virtual_extruder;
    virtual_extruder.id = this->next_free_virtual_id();
    for (size_t i = 0; i < preset.extruder_ids_1based.size(); ++i) {
        virtual_extruder.components.push_back(
            {preset.extruder_ids_1based.at(i),
             static_cast<double>(preset.ratios_percent.at(i)) / 100.0}
        );
    }

    m_working_virtual_extruders.push_back(std::move(virtual_extruder));

    this->rebuild_virtual_extruder_list(-1);
}

unsigned int ColorMixDialog::next_free_virtual_id() const
{
    std::set<unsigned int> used_ids;
    for (const VirtualExtruder& virtual_extruder : std::as_const(m_working_virtual_extruders)) {
        used_ids.insert(virtual_extruder.id);
    }

    // The painted facet states are shared, so skip the ids of every printer group.
    unsigned int candidate = std::max(m_physical_extruder_count, m_max_physical_slot_count) + 1;
    while (used_ids.contains(candidate) || m_reserved_virtual_ids.contains(candidate)) {
        ++candidate;
    }

    return candidate;
}

int ColorMixDialog::selected_index() const
{
    if (m_selected_index < 0
        || m_selected_index >= static_cast<int>(m_working_virtual_extruders.size()))
    {
        return -1;
    }

    return m_selected_index;
}

int ColorMixDialog::find_matching_blend_index(const BlendPreset& preset) const
{
    struct IdRatio
    {
        unsigned int id;
        double ratio;
    };

    const auto by_id_then_ratio = [](const IdRatio& a, const IdRatio& b)
    { return a.id != b.id ? a.id < b.id : a.ratio < b.ratio; };

    std::vector<IdRatio> preset_sorted;
    preset_sorted.reserve(preset.extruder_ids_1based.size());
    for (size_t i = 0; i < preset.extruder_ids_1based.size(); ++i) {
        preset_sorted.push_back(
            {preset.extruder_ids_1based.at(i),
             static_cast<double>(preset.ratios_percent.at(i)) / 100.0}
        );
    }

    std::ranges::sort(preset_sorted, by_id_then_ratio);

    for (size_t recipe_index = 0; recipe_index < m_working_virtual_extruders.size(); ++recipe_index)
    {
        const VirtualExtruder& virtual_extruder = m_working_virtual_extruders.at(recipe_index);
        if (virtual_extruder.type() != VirtualExtruder::Type::Blend) {
            continue;
        }

        if (virtual_extruder.components.size() != preset.extruder_ids_1based.size()) {
            continue;
        }

        std::vector<IdRatio> recipe_sorted;
        recipe_sorted.reserve(virtual_extruder.components.size());
        for (const VirtualExtruderComponent& component : virtual_extruder.components) {
            recipe_sorted.push_back({component.extruder_id, component.ratio});
        }

        std::ranges::sort(recipe_sorted, by_id_then_ratio);

        bool match = true;
        for (size_t i = 0; i < recipe_sorted.size() && match; ++i) {
            if (recipe_sorted[i].id != preset_sorted[i].id) {
                match = false;
            } else if (std::abs(recipe_sorted[i].ratio - preset_sorted[i].ratio) > 0.02) {
                match = false;
            }
        }

        if (match) {
            return static_cast<int>(recipe_index);
        }
    }

    return -1;
}

int ColorMixDialog::count_objects_using(unsigned int virtual_extruder_id) const
{
    const std::map<unsigned int, int>::const_iterator it =
        m_object_use_counts.find(virtual_extruder_id);
    return it == m_object_use_counts.end() ? 0 : it->second;
}

ImColor ColorMixDialog::physical_color(unsigned int extruder_id_1based) const
{
    return physical_slot_color(m_physical_colors, extruder_id_1based);
}

void ColorMixDialog::clear_color_override()
{
    const int selection = this->selected_index();
    if (selection < 0) {
        return;
    }

    m_working_virtual_extruders.at(selection).color.reset();
}

unsigned int ColorMixDialog::pick_unused_physical_id(const VirtualExtruder& virtual_extruder) const
{
    std::set<unsigned int> used_ids;
    for (const VirtualExtruderComponent& component : virtual_extruder.components) {
        used_ids.insert(component.extruder_id);
    }

    for (unsigned int candidate = 1; candidate <= m_physical_extruder_count; ++candidate) {
        if (!used_ids.contains(candidate)) {
            return candidate;
        }
    }

    return 1u;
}

} // namespace Slic3r::App::ColorMix
