#include "Slic3r/App/PrinterSettingsDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/PhysicalPrinterForm.hpp"
#include "Slic3r/App/WarningPanel.hpp"
#include "Slic3r/App/PrinterSearchFunction.hpp"

#include <algorithm>

using Slic3r::Biz::Preset::HwItemType;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::SelectionId;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App {

PrinterSettingsDialog::PrinterSettingsDialog(
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
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_preset_searcher(
        std::make_shared<Biz::ObservableListSearcher<Biz::PhysicalPrinter::PhysicalPrinterConfig>>(
            score_physical_printer
        )
    ),
    m_printer_filter(
        std::make_shared<
            Biz::ObservableListSortFilter<Biz::PhysicalPrinter::PhysicalPrinterConfig>>()
    ),
    m_printer_add_dialog(printer_add_dialog)
{
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

void PrinterSettingsDialog::on_preset_selection_changed(
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

void PrinterSettingsDialog::on_config_container_selection_changed(
    SelectionId project_id,
    SelectionId config_container_id
)
{
    this->update_warning();
}

void PrinterSettingsDialog::on_preset_bundles_loaded()
{
    update_warning();
}

void PrinterSettingsDialog::update_warning()
{
    m_warning->set_visible(m_project_interactor.preset_interactor().has_invalid_hw_config());
}

void PrinterSettingsDialog::on_list_selection_changed(Domain::SelectionId new_selection)
{
    if (new_selection == Domain::INVALID_ID) {
        return;
    }

    update_settings_data();
}

void PrinterSettingsDialog::on_selected_project_changed_final(size_t index)
{
    if (index == Domain::INVALID_ID) {
        return;
    }

    // Once we will start to implement remembering Dialog context in Navigator
    // This should be cleaned up to open proper page instead of defaulting to list
    m_stack_layout->set_current_index(0);
}

void PrinterSettingsDialog::select_page_settings(
    const PhysicalPrinter::PhysicalPrinterConfig& printer
)
{
    // Runtime printers are not configurable
    if (printer.is_runtime) {
        return;
    }
    m_physical_printer_form->set_printer_config(printer);
    update_warning();
    m_stack_layout->set_current_index(1);
}

void PrinterSettingsDialog::create_page_list()
{
    ASSERT(!m_page_list);
    constexpr const Unit Padding{20_fpx};

    m_page_list = m_stack_layout->emplace_back<Item>();
    m_page_list->set_orientation(Orientation::Vertical);
    m_page_list->set_gap(10_fpx);
    m_page_list->set_padding(Paddings{Padding, 10_fpx, Padding, Padding});

    m_printer_filter_type = static_cast<PrinterFilter>(
        AppServices::instance().app_config().get<int>("last_printer_settings_dialog_filter")
    );

    Item* filters_row = m_page_list->emplace_back<Item>();
    filters_row->set_gap(5_fpx);
    for (const std::string& filter_btn_name :
         std::initializer_list<std::string>{_u8L("All"), _u8L("Local"), _u8L("Connect")})
    {
        LayoutButton* btn = filters_row->emplace_back<LayoutButton>(filter_btn_name);
        btn->set_checkable(true);
        btn->set_checked(
            m_printer_filter_buttons.size() == static_cast<size_t>(m_printer_filter_type)
        );
        btn->set_content_padding(Paddings{10_fpx, 4_fpx});
        m_printer_filter_buttons.push_back(btn);
        m_printer_filter_button_group.insert_button(btn);
    }
    m_printer_filter_button_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton*)
    {
        const auto it = std::ranges::find(m_printer_filter_buttons, current_checked);
        m_printer_filter_type =
            static_cast<PrinterFilter>(std::distance(m_printer_filter_buttons.begin(), it));
        AppServices::instance().app_config_interactor().set_item_value(
            "last_printer_settings_dialog_filter",
            Domain::ConfigValue{static_cast<int>(m_printer_filter_type)}
        );
        m_printer_filter->invalidate();
    };

    m_printer_filter->set_filter_fn(
        [this](const PhysicalPrinter::PhysicalPrinterConfig& printer) -> bool
        {
            const bool is_connect =
                std::holds_alternative<PhysicalPrinter::ConnectUpload>(printer.payload);
            switch (m_printer_filter_type) {
            case PrinterFilter::Local:
                return !is_connect;
            case PrinterFilter::Connect:
                return is_connect;
            case PrinterFilter::All:
                break;
            }
            return true;
        }
    );

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

    Item* content_area = m_page_list->emplace_back<Item>();
    content_area->set_gap(5_fpx);
    content_area->set_orientation(Orientation::Vertical);

    Separator* sep_top = content_area->emplace_back<Separator>(Orientation::Horizontal);
    sep_top->set_margin(Margins{-Padding, 0});

    // Create the ViewFactory explicitly:
    auto factory = PrinterListViewFactory(
        [this](size_t index)
        {
            m_project_interactor.physical_printer_interactor().select_uuid(
                m_preset_searcher->at(index).uuid
            );
            close_action();
        },
        [this](size_t index) { select_page_settings(m_preset_searcher->at(index)); },
        [this](size_t index)
        {
            auto& dlg_manager = App::AppServices::instance().dialog_manager();
            dlg_manager.show_yesno_dialog(
                _u8L("Remove printer?"),
                _u8L("Do you wish to remove the printer?"),
                [this, index](bool checked)
                {
                    if (checked) {
                        m_project_interactor.physical_printer_interactor().remove_uuid(
                            m_preset_searcher->at(index).uuid
                        );
                    }
                }
            );
        },
        m_project_interactor.preset_interactor(),
        m_project_interactor.physical_printer_interactor()
    );
    m_printer_list_view = m_page_list->emplace_back<PrinterListView>(std::move(factory));
    m_printer_list_view->set_flex_grow(1);
    m_printer_list_view->set_padding(Paddings(0, 0, 15_fpx, 0));
    m_printer_list_view->set_margin(Margins(0, 0, -20_fpx, 0));
    m_printer_list_view->set_gap(8_fpx);
    m_printer_list_view->set_max_height(275);
    m_printer_list_view->set_orientation(Orientation::Vertical);
    m_printer_list_view->set_source_list(m_preset_searcher.get());

    m_printer_filter->set_source_model(
        &m_project_interactor.physical_printer_interactor().observable_list()
    );
    m_preset_searcher->set_source_model(m_printer_filter.get());

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
    // m_printer_add_dialog->callbacks().printer_added = [this] { m_preset_searcher->invalidate(); };

    m_add_printer_button = row->emplace_back<LayoutButton>(_u8L("Add printer"));
    m_add_printer_button->set_content_padding({7_fpx, 2_fpx});
    m_add_printer_button->set_height(30_fpx);
    m_add_printer_button->callbacks().action = [this] { m_printer_add_dialog->open(); };
}

void PrinterSettingsDialog::create_page_settings()
{
    ASSERT(!m_page_settings);
    m_page_settings = m_stack_layout->emplace_back<ScrollArea>();
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
    // Page is scrollable, its content must not shrink to fit
    m_printer_icon->set_flex_shrink(0);

    m_physical_printer_form =
        m_page_settings->emplace_back<PhysicalPrinterForm>(m_project_interactor);
    m_physical_printer_form->set_margin({-20_fpx, 0});
    m_physical_printer_form->set_flex_shrink(0);
    m_physical_printer_form->callbacks().printer_saved =
        [this](const Biz::PhysicalPrinter::PhysicalPrinterConfig& printer)
    {
        m_project_interactor.physical_printer_interactor().create_or_save_physical_printer(printer);
        close_action();
    };

    m_warning = m_page_settings->emplace_back<WarningPanel>();
    m_warning->set_warning(
        Biz::_u8L("Invalid combination"),
        Biz::_u8L("Selected nozzles cannot be used together on any layer height.")
    );
}

void PrinterSettingsDialog::on_about_to_show()
{
    m_stack_layout->set_current_index(0);
    update_warning();
}

void PrinterSettingsDialog::update_settings_data()
{
    const Domain::Preset::HwPrinterConfig& printer_config =
        m_project_interactor.preset_interactor().current_printer_config();

    m_text_printer_name->set_text(printer_config.name);

    if (printer_config.visual.thumbnail.has_value()) {
        const std::string image_path =
            printer_config.relative_path_to_assets() + printer_config.visual.thumbnail.value();

        m_printer_icon->set_image(image_path);
    }
}

void PrinterSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

} // namespace Slic3r::App
