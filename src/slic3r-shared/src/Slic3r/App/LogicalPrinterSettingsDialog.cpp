#include "Slic3r/App/LogicalPrinterSettingsDialog.hpp"

#include "Slic3r/App/PrinterSearchFunction.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/ColorMix/ColorMixDialog.hpp"
#include "Slic3r/App/PrinterAdvancedSettingsDialog.hpp"
#include "Slic3r/App/WarningPanel.hpp"

using Slic3r::App::ColorMix::ColorMixDialog;
using Slic3r::Biz::Preset::HwItemType;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::SelectionId;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App {

LogicalPrinterSettingsDialog::LogicalPrinterSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    PrinterAddDialog* printer_add_dialog,
    Navigator& navigator
) :
    Dialog({"Printers"}, "LogicalPrinterSettingsDialog"),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_preset_list_selection_changed_listener_scope(
        project_interactor.preset_interactor().printer_presets(),
        *this
    ),
    m_selected_project_changed_listener_scope(project_interactor, *this),
    m_app_config_changed_listener_scope(AppServices::instance().app_config_interactor(), *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_preset_favorite_filter(
        std::make_shared<Biz::ObservableListSortFilter<Biz::Preset::PresetItem>>()
    ),
    m_preset_searcher(
        std::make_shared<Biz::ObservableListSearcher<Biz::Preset::PresetItem>>(score_preset_item)
    ),
    m_printer_add_dialog(printer_add_dialog)
{
    m_preset_favorite_filter->set_filter_fn(
        [this](const Biz::Preset::PresetItem& item) -> bool
        {
            return m_only_favorites_button->checked() ?
                AppServices::instance()
                    .app_config()
                    .app_settings_advanced()
                    .contains_printer_favorite_preset(item.id, item.hw_printer_config_id) :
                true;
        }
    );

    m_advanced_dialog = content_item()->emplace_back<PrinterAdvancedSettingsDialog>(
        project_interactor,
        m_navigator,
        this
    );

    m_color_mix_dialog =
        content_item()->emplace_back<ColorMixDialog>(project_interactor, m_navigator, this);

    content_item()->set_width(350_fpx);

    content()->set_padding(0);
    content()->set_orientation(Orientation::Vertical);

    m_stack_layout = content()->emplace_back<StackLayout>();
    m_stack_layout->set_orientation(Orientation::Vertical);

    create_page_list();

    create_page_settings();

    m_stack_layout->set_current_index(0);

    // Default project is already loaded, update default printer selection
    on_list_selection_changed(
        m_project_interactor.preset_interactor().printer_presets().selected_index()
    );
}

void LogicalPrinterSettingsDialog::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id
        && type == Biz::Preset::PresetItemType::PrinterPreset)
    {
        update_settings_data();
    }
}

void LogicalPrinterSettingsDialog::on_hw_item_selection_changed(
    SelectionId project_id,
    SelectionId config_container_id,
    HwItemType type
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id
        && type == HwItemType::ToolItem)
    {
        this->update_color_mix_visibility();
    }
}

void LogicalPrinterSettingsDialog::
    on_config_container_selection_changed(SelectionId project_id, SelectionId config_container_id)
{
    this->update_warning();
    this->update_color_mix_visibility();
}

void LogicalPrinterSettingsDialog::update_warning()
{
    m_warning->set_visible(m_project_interactor.preset_interactor().has_invalid_hw_config());
}

void LogicalPrinterSettingsDialog::on_list_selection_changed(Domain::SelectionId new_selection)
{
    if (new_selection == Domain::INVALID_ID) {
        return;
    }

    for (size_t button_index = 0; button_index < m_printer_list_view->object_count();
         ++button_index)
    {
        LogicalPrinterSettingsButton* button = dynamic_cast<LogicalPrinterSettingsButton*>(
            m_printer_list_view->get_item(button_index)
        );
        ASSERT(button);
        button->set_checked(new_selection == button_index);
    }

    update_settings_data();
}

void LogicalPrinterSettingsDialog::on_selected_project_changed_final(size_t index)
{
    if (index == Domain::INVALID_ID) {
        return;
    }

    // Once we will start to implement remembering Dialog context in Navigator
    // This should be cleaned up to open proper page instead of defaulting to list
    m_stack_layout->set_current_index(0);
}

PrinterAdvancedSettingsDialog& LogicalPrinterSettingsDialog::printer_advanced_settings_dialog()
{
    return *m_advanced_dialog;
}

ColorMixDialog& LogicalPrinterSettingsDialog::color_mix_dialog()
{
    return *m_color_mix_dialog;
}

void LogicalPrinterSettingsDialog::select_page_settings()
{
    update_warning();
    m_stack_layout->set_current_index(1);
}

void LogicalPrinterSettingsDialog::on_app_config_changed(const std::string& key)
{
    if (key == "printers_only_favorites") {
        m_only_favorites_button->set_checked(
            AppServices::instance()
                .app_config()
                .get_config_box()
                .items.find("printers_only_favorites")
                ->value()
                .get<bool>()
        );
        m_add_printer_button->set_visible(m_only_favorites_button->checked());
    }
}

void LogicalPrinterSettingsDialog::create_page_list()
{
    ASSERT(!m_page_list);
    constexpr const Unit Padding{20_fpx};

    m_page_list = m_stack_layout->emplace_back<Item>();
    m_page_list->set_orientation(Orientation::Vertical);
    m_page_list->set_gap(10_fpx);
    m_page_list->set_padding(Paddings{Padding, 10_fpx, Padding, Padding});

    Item* search_row = m_page_list->emplace_back<Item>();
    search_row->set_gap(5_fpx);
    Icon* icon = search_row->emplace_back<Icon>(Render::Icon::Search);
    icon->set_width(16_fpx);
    icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_input_text_search = search_row->emplace_back<InputText>();
    m_input_text_search->set_hint(_u8L("Search..."));
    m_input_text_search->set_flex_grow(1);
    m_input_text_search->callbacks().text_changed = [this]
    { m_preset_searcher->set_search_text(m_input_text_search->text()); };

    m_only_favorites_button = search_row->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::Star,
        Biz::_u8L("Only favorites")
    );
    m_only_favorites_button->set_width(24_fpx);
    m_only_favorites_button->set_height(24_fpx);
    m_only_favorites_button->set_checkable(true);
    m_only_favorites_button->set_self_align(YGAlignCenter);

    const bool only_favorites = AppServices::instance()
                                    .app_config()
                                    .get_config_box()
                                    .items.find("printers_only_favorites")
                                    ->value()
                                    .get<bool>();
    m_only_favorites_button->set_checked(only_favorites);
    m_only_favorites_button->set_icon(m_only_favorites_button->checked() ? Render::Icon::StarSolid : Render::Icon::Star);

    m_only_favorites_button->callbacks().checked_changed = [this](bool checked)
    {
        m_only_favorites_button->set_icon(checked ? Render::Icon::StarSolid : Render::Icon::Star);
        m_only_favorites_button->set_tooltip(
            checked ? Biz::_u8L("Show all items") : Biz::_u8L("Filter only favorited items")
        );
        m_preset_favorite_filter->invalidate();
        if (AppServices::instance()
                .app_config()
                .get_config_box()
                .items.find("printers_only_favorites")
                ->value()
                .get<bool>()
            != checked)
        {
            AppServices::instance().app_config_interactor().set_item_value(
                "printers_only_favorites",
                Domain::ConfigValue{checked}
            );
        }
    };

    Item* content_area = m_page_list->emplace_back<Item>();
    content_area->set_gap(5_fpx);
    content_area->set_orientation(Orientation::Vertical);

    Separator* sep_top = content_area->emplace_back<Separator>(Orientation::Horizontal);
    sep_top->set_margin(Margins{-Padding, 0});

    // Create the ViewFactory explicitly:
    auto factory = PrinterListViewFactory(
        [this](size_t index)
        {
            auto& preset_interactor = m_project_interactor.preset_interactor();
            const Biz::Preset::PresetItem& item =
                m_printer_list_view->item_at(index)->preset_item();

            if (!Biz::Preset::PresetSelectionCheck::can_select_printer_preset(
                    preset_interactor,
                    item.hw_printer_config_id,
                    item.id
                ))
            {
                return;
            }

            preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id, true);
            m_navigator.set_opened_dialog(nullptr);
            m_project_interactor.undo_provider().take_snapshot(
                UndoSnapshotType::SelectPrinterPreset
            );
        },
        [this](size_t index)
        {
            auto& preset_interactor = m_project_interactor.preset_interactor();
            const Biz::Preset::PresetItem& item =
                m_printer_list_view->item_at(index)->preset_item();

            auto selected_preset = preset_interactor.selected_printer_preset();
            if (selected_preset.hw_config.id == item.hw_printer_config_id
                && selected_preset.printer.id == item.id)
            {
                select_page_settings();
                return;
            }

            if (!Biz::Preset::PresetSelectionCheck::can_select_printer_preset(
                    preset_interactor,
                    item.hw_printer_config_id,
                    item.id
                ))
            {
                return;
            }

            preset_interactor.select_printer_preset(item.hw_printer_config_id, item.id, true);
            select_page_settings();
            m_project_interactor.undo_provider().take_snapshot(
                UndoSnapshotType::SelectPrinterPreset
            );
        },
        [this](size_t index) { m_preset_favorite_filter->invalidate(); },
        m_project_interactor.preset_interactor()
    );

    m_printer_list_view = content_area->emplace_back<PrinterListView>(std::move(factory));
    m_printer_list_view->set_flex_grow(1);
    m_printer_list_view->set_max_height(275_fpx);
    m_printer_list_view->set_min_height(275_fpx);
    m_printer_list_view->set_margin(Margins(0, 0, -20_fpx, 0));
    m_printer_list_view->set_padding(Paddings(0, 0, 20, 0));
    m_printer_list_view->set_gap(10_fpx);
    m_printer_list_view->set_orientation(Orientation::Vertical);
    m_printer_list_view->set_source_list(m_preset_searcher.get());
    m_preset_searcher->set_source_model(m_preset_favorite_filter.get());
    m_preset_favorite_filter->set_source_model(
        &m_project_interactor.preset_interactor().printer_presets().items()
    );

    Separator* sep_bottom = content_area->emplace_back<Separator>(Orientation::Horizontal);
    sep_bottom->set_margin(Margins{-Padding, 0});

    Item* row = m_page_list->emplace_back<Item>();
    row->set_justify_content(YGJustifySpaceBetween);

    LayoutButton* show_diff_dialog_button =
        row->emplace_back<LayoutButton>(_u8L("Compare presets"));
    show_diff_dialog_button->set_content_padding({7_fpx, 2_fpx});
    show_diff_dialog_button->set_height(30_fpx);
    show_diff_dialog_button->callbacks().action = [this]
    {
        auto& dlg_manager = App::AppServices::instance().dialog_manager();
        dlg_manager.show_diff_dialog(m_project_interactor.preset_interactor());
    };

    m_printer_add_dialog->attach_to_item(content_item(), Position::Left);
    m_printer_add_dialog->callbacks().printer_added = [this]
    { m_preset_favorite_filter->invalidate(); };

    m_add_printer_button = row->emplace_back<LayoutButton>(_u8L("Add printer"));
    m_add_printer_button->set_content_padding({7_fpx, 2_fpx});
    m_add_printer_button->set_height(30_fpx);
    m_add_printer_button->callbacks().action = [this] { m_printer_add_dialog->open(); };
    m_add_printer_button->set_visible(only_favorites);
}

void LogicalPrinterSettingsDialog::create_page_settings()
{
    ASSERT(!m_page_settings);
    m_page_settings = m_stack_layout->emplace_back<Item>();
    m_page_settings->set_orientation(Orientation::Vertical);
    m_page_settings->set_gap(5_fpx);
    m_page_settings->set_padding(Paddings{20_fpx, 10_fpx, 20_fpx, 20_fpx});

    Item* title_row = m_page_settings->emplace_back<Item>();
    title_row->set_align_items(YGAlignCenter);
    title_row->set_gap(5_fpx);
    LayoutButton* back_button =
        title_row->emplace_back<LayoutButton>(std::string{}, Render::Icon::CaretLeft);
    back_button->set_width(24_fpx);
    back_button->set_height(24_fpx);
    back_button->set_content_padding(3_fpx);
    back_button->callbacks().action = [this]() { m_stack_layout->set_current_index(0); };
    m_text_printer_name             = title_row->emplace_back<Text>("Unknown");
    m_text_printer_name->set_flex_grow(1);
    m_text_printer_name->set_wrap_mode(Text::WrapMode::WrapElide);

    Separator* sep = m_page_settings->emplace_back<Separator>(Orientation::Horizontal);
    sep->set_margin(Margins{-20_fpx, 0});

    m_printer_icon = m_page_settings->emplace_back<Icon>(Render::Icon::None);
    m_printer_icon->set_height(225_fpx);
    m_printer_icon->set_margin({0, 5_fpx});
    m_printer_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);

    m_page_settings->emplace_back<Text>(_u8L("Sheet"), Render::ImguiFontType::Bold);
    m_combo_sheets =
        m_page_settings
            ->emplace_back<Yoga::ComboBoxListViewSelection<Domain::Preset::HwSheetConfigDef>>();
    m_combo_sheets->set_get_name_fn(
        [](const Domain::Preset::HwSheetConfigDef* data) -> std::string { return data->name; }
    );
    m_combo_sheets->set_source_list(&m_project_interactor.preset_interactor().sheet_items());
    m_combo_sheets->callbacks().selection_changed = [this](int sheet_index)
    {
        if (sheet_index >= 0) {
            m_project_interactor.preset_interactor().select_printer_sheet(
                m_project_interactor.preset_interactor().sheet_items().items().at(sheet_index).id,
                true
            );
        }
    };

    Text* label = m_page_settings->emplace_back<Text>(_u8L("Nozzles"), Render::ImguiFontType::Bold);
    label->set_margin(Margins(0, 10_fpx, 0, 0));

    auto validation_updated = [this](bool valid) { m_warning->set_visible(!valid); };
    m_nozzle_list_view =
        m_page_settings->emplace_back<NozzleListView>(NozzleListView::ViewFactoryType{
            m_project_interactor.preset_interactor(),
            validation_updated
        });
    m_nozzle_list_view->set_orientation(Orientation::Vertical);
    m_nozzle_list_view->set_gap(5_fpx);
    m_nozzle_list_view->set_source_list(&m_project_interactor.preset_interactor().tool_items());

    m_warning = m_page_settings->emplace_back<WarningPanel>();
    m_warning->set_warning(
        Biz::_u8L("Invalid combination"),
        Biz::_u8L("Selected nozzles cannot be used together on any layer height.")
    );

    m_advanced_dialog->attach_to_item(content_item(), Position::Left);

    LayoutButton* button_advanced_setting =
        m_page_settings->emplace_back<LayoutButton>(_u8L("Advanced settings"), Render::Icon::Cog);
    button_advanced_setting->set_content_padding({0.f, 7_fpx});
    button_advanced_setting->set_height(30_fpx);
    button_advanced_setting->callbacks().action = [this]
    {
        if (m_advanced_dialog->opened()) {
            m_navigator.set_opened_dialog(this);
        } else {
            m_navigator.set_opened_dialog(m_advanced_dialog);
        }
    };
    m_advanced_dialog->callbacks().opened = [button_advanced_setting]
    { button_advanced_setting->set_checked(true); };
    m_advanced_dialog->callbacks().closed = [button_advanced_setting]
    { button_advanced_setting->set_checked(false); };

    m_color_mix_dialog->attach_to_center();

    m_color_mix_button =
        m_page_settings->emplace_back<LayoutButton>(_u8L("Virtual extruders - Color mix"));
    m_color_mix_button->set_content_padding({0.f, 7_fpx});
    m_color_mix_button->set_height(30_fpx);
    m_color_mix_button->callbacks().action = [this]
    {
        if (m_color_mix_dialog->opened()) {
            m_navigator.set_opened_dialog(this);
        } else {
            m_navigator.set_opened_dialog(m_color_mix_dialog);
        }
    };
    m_color_mix_dialog->callbacks().opened = [this] { m_color_mix_button->set_checked(true); };
    m_color_mix_dialog->callbacks().closed = [this] { m_color_mix_button->set_checked(false); };

    this->update_color_mix_visibility();
}

void LogicalPrinterSettingsDialog::on_about_to_show()
{
    m_stack_layout->set_current_index(0);
    update_warning();
}

void LogicalPrinterSettingsDialog::update_settings_data()
{
    const Domain::Preset::HwPrinterConfig& printer_config =
        m_project_interactor.preset_interactor().current_printer_config();

    m_text_printer_name->set_text(printer_config.name);

    if (printer_config.visual.thumbnail.has_value()) {
        const std::string image_path =
            printer_config.relative_path_to_assets() + printer_config.visual.thumbnail.value();

        m_printer_icon->set_image(image_path);
    }

    update_color_mix_visibility();
}

void LogicalPrinterSettingsDialog::update_color_mix_visibility()
{
    bool can_mix_colors = false;
    if (m_project_interactor.selected_config_container_id() != Domain::INVALID_ID) {
        const ConfigContainer& config_container = m_project_interactor.selected_config_container();

        can_mix_colors = config_container.print_technology() == PrinterTechnology::FFF
            && config_container.selected_preset().hw_config.material_slot_count() > 1;
    }

    m_color_mix_button->set_visible(can_mix_colors);

    if (!can_mix_colors && m_color_mix_dialog->opened()) {
        m_navigator.set_opened_dialog(this);
    }
}

void LogicalPrinterSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

} // namespace Slic3r::App
