#include "Slic3r/App/SidebarPrint.hpp"

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/RadioButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/PrintSettingsDialog.hpp"

#include "Slic3r/App/Config/PrintToolFavoritesItem.hpp"
#include "Slic3r/App/Config/ToolToFeatureDialog.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"

#include <Slic3r/Log.hpp>

#include <imgui/imgui.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::App::Render;

namespace Slic3r::App {

SidebarPrint::SidebarPrint(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Window("SidebarPrint"),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    set_transparent(true);
    m_print_settings_dialog = emplace_back<PrintSettingsDialog>(project_interactor, m_navigator);

    m_tool_to_feature_dialog = emplace_back<ToolToFeatureDialog>(m_project_interactor);

    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);

    Paddings pad = padding().source;
    pad.right    = 0;
    set_padding(pad);

    set_flex_grow(1);

    m_content_area = emplace_back<ScrollArea>();
    m_content_area->set_orientation(Orientation::Vertical);
    m_content_area->set_flex_grow(1);
    m_content_area->set_padding(Paddings(0, 0, 1_rem, 0));
    m_content_area->set_gap(5_fpx);

    m_print_settings_dialog->attach_to_item(this, Position::Left, 20_fpx);
    m_print_settings_dialog->callbacks().closed = [this]()
    { m_settings_set_btn->set_checked(false); };

    Item* layer_height_row = m_content_area->emplace_back<Item>();
    layer_height_row->set_flex_shrink(0);
    Rectangle* text_rect = layer_height_row->emplace_back<Rectangle>();
    text_rect->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    text_rect->set_align_items(YGAlignCenter);
    text_rect->set_flags(ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft);
    text_rect->set_padding(Paddings(5_fpx, 0));
    text_rect->emplace_back<Text>(Biz::_u8L("Print preset"));

    m_combo_print =
        layer_height_row->emplace_back<ComboBoxListViewSelection<Biz::Preset::PresetItem>>();
    m_combo_print->set_get_name_fn(
        [](const Biz::Preset::PresetItem* item) -> std::string
        {
            return item->ui_preset_name();
        }
    );
    m_combo_print->set_source_list(&m_project_interactor.preset_interactor().print_presets());
    m_combo_print->set_flex_grow(1);
    m_combo_print->callbacks().selection_changed = [this](int print_index)
    {
        if (print_index >= 0) {
            auto& preset_interactor = m_project_interactor.preset_interactor();
            const std::string& preset_id =
                preset_interactor.print_presets().items().at(print_index).id;
            if (Biz::Preset::PresetSelectionCheck::can_select_print_preset(
                    preset_interactor,
                    preset_id
                ))
            {
                preset_interactor.select_print_preset(preset_id, true);
                m_last_selected_index = print_index;
            } else {
                m_combo_print->set_current_index(m_last_selected_index);
            }
        }
    };
    refresh_print_combobox_label_color();

    m_settings_set_btn =
        layer_height_row->emplace_back<LayoutButton>(std::string{}, Render::Icon::Cog);
    m_settings_set_btn->set_checkable(true);
    m_settings_set_btn->set_height(24_fpx);
    m_settings_set_btn->set_self_align(YGAlignCenter);
    m_settings_set_btn->set_background_color(Platform::Color::ButtonTransparent);
    m_settings_set_btn->callbacks().action = [this]()
    {
        m_navigator.set_opened_dialog(
            m_settings_set_btn->checked() ? m_print_settings_dialog : nullptr
        );
    };

    m_tool_head_list_view = m_content_area->emplace_back<ToolHeadListView>(
        Yoga::ViewFactory<
            SidebarToolHeadRow,
            Biz::Preset::PresetItemObservableList,
            Biz::ProjectInteractor&>{m_project_interactor}
    );
    m_tool_head_list_view->set_orientation(Orientation::Vertical);
    m_tool_head_list_view->set_gap(5_fpx);
    m_tool_head_list_view->set_flex_shrink(0);
    m_tool_head_list_view->set_source_list(
        &m_project_interactor.preset_interactor().tool_presets()
    );

    update_tools_visibility();
    refresh_tools_comboboxes_label_colors();

    m_tool_to_feature_dialog->attach_to_item(this, Position::Left, 20_fpx);

    add_separator();

    create_tool_to_feature_row();
    update_tool_to_feature_row_visibility();

    create_favorite_params();
}

void SidebarPrint::add_separator()
{
    Separator* separator = m_content_area->emplace_back<Separator>();
    separator->set_margin(Margins(-padding().left, gap(), -padding().right, gap()));
}

void SidebarPrint::create_tool_to_feature_row()
{
    m_tool_to_feature_row = m_content_area->emplace_back<Item>();
    m_tool_to_feature_row->set_orientation(Orientation::Horizontal);
    m_tool_to_feature_row->set_padding({0, 5_fpx});
    m_tool_to_feature_row->set_gap(5_fpx);
    m_tool_to_feature_row->set_height(40_fpx);
    m_tool_to_feature_row->set_flex_shrink(0);

    Text* tool_to_feature_label =
        m_tool_to_feature_row->emplace_back<Text>(Biz::_u8L("Tool to feature"));
    tool_to_feature_label->set_wrap_mode(Text::WrapMode::WrapElide);
    tool_to_feature_label->set_align({AlignH::Left, AlignV::Center});
    tool_to_feature_label->set_flex_grow(1);
    tool_to_feature_label->set_width(90);
    tool_to_feature_label->set_max_width(175);

    LayoutButton* configure_button =
        m_tool_to_feature_row->emplace_back<LayoutButton>(Biz::_u8L("Configure"));
    configure_button->set_flex_grow(1);
    configure_button->set_self_align(YGAlignCenter);
    configure_button->set_max_width(200_fpx);
    configure_button->set_width(90_fpx);
    configure_button->set_height(25_fpx);

    configure_button->callbacks().action = [this]() { m_tool_to_feature_dialog->open(); };
}

void SidebarPrint::create_favorite_params()
{
    m_favorite_params = m_content_area->emplace_back<PrintToolFavoritesItem>(m_project_interactor);
}

void SidebarPrint::update_tools_visibility()
{
    const bool is_multi_extruder =
        Domain::Preset::get_feature<bool>(
            m_project_interactor.preset_interactor().selected_printer_preset().hw_config.features,
            "multi_extruder"
        )
            .value_or(false);

    m_tool_head_list_view->set_visible(is_multi_extruder);
}

void SidebarPrint::update_tool_to_feature_row_visibility()
{
    m_tool_to_feature_row->set_visible(
        m_project_interactor.preset_interactor().selected_printer_preset().materials.size() > 1
    );
}

void SidebarPrint::refresh_print_combobox_label_color()
{
    bool is_modified_preset = m_project_interactor.preset_interactor()
                                  .print_tool_cbi()
                                  .observable_list()
                                  .lock()
                                  ->is_dirty_print();
    m_combo_print->set_label_color(m_theme->color_imgui(
        is_modified_preset ? Platform::Color::AccentTertiary : Platform::Color::Text
    ));
}

void SidebarPrint::refresh_tools_comboboxes_label_colors()
{
    const size_t tool_cnt = m_project_interactor.preset_interactor().tool_presets().size();
    for (size_t tool_index{}; tool_index < tool_cnt; tool_index++) {
        bool is_modified_preset = m_project_interactor.preset_interactor()
                                      .print_tool_cbi()
                                      .observable_list()
                                      .lock()
                                      ->is_dirty_tool(tool_index);
        m_tool_head_list_view->item_at(tool_index)
            ->combo_box()
            ->set_label_color(m_theme->color_imgui(
                is_modified_preset ? Platform::Color::AccentTertiary : Platform::Color::Text
            ));
    }
}

PrintSettingsDialog& SidebarPrint::print_settings_dialog()
{
    return *m_print_settings_dialog;
}

ToolToFeatureDialog* SidebarPrint::tool_to_feature_dialog()
{
    return m_tool_to_feature_dialog;
}

void SidebarPrint::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id)
    {
        switch (type) {
        case Biz::Preset::PresetItemType::PrinterPreset:
            update_tools_visibility();
            update_tool_to_feature_row_visibility();
            break;
        case Biz::Preset::PresetItemType::PrintPreset:
            refresh_print_combobox_label_color();
            break;
        case Biz::Preset::PresetItemType::ToolPrintPreset:
            refresh_tools_comboboxes_label_colors();
            break;
        case Biz::Preset::PresetItemType::MaterialPreset:
            update_tool_to_feature_row_visibility();
            break;
        default:
            break;
        }
    }
}

void SidebarPrint::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{std::get<Domain::FDMConfigLocation>(item.location())};
        if (location == Domain::FDMConfigLocation::Print) {
            refresh_print_combobox_label_color();
        } else if (location == Domain::FDMConfigLocation::Tool) {
            refresh_tools_comboboxes_label_colors();
        }
    } else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{std::get<Domain::SLAConfigLocation>(item.location())};
        if (location != Domain::SLAConfigLocation::Print) {
            refresh_print_combobox_label_color();
        }
    }
}

void SidebarPrint::on_preset_bundles_loaded()
{
    refresh_print_combobox_label_color();
    refresh_tools_comboboxes_label_colors();
}

void SidebarPrint::on_config_container_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id)
    {
        update_tools_visibility();
        update_tool_to_feature_row_visibility();
    }
}

} // namespace Slic3r::App
