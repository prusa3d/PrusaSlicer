#include "Slic3r/App/PhysicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterAdvancedSettingsDialog.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/IDialogManager.hpp"
#include <Slic3r/App/AppServices.hpp>

#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::PhysicalPrinter;

namespace Slic3r::App {

PhysicalPrinterSettingsDialog::PhysicalPrinterSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    PhysicalPrinterAdvancedSettingsDialog* advanced_dialog
) :
    Dialog({"Destination"}, "PhysicalPrinterSettingsDialog"),
    m_physical_printer_changed_listener_scope(project_interactor.physical_printer_interactor(), *this),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_user_account_listener_scope(project_interactor.user_account_interactor(), *this),
    m_project_interactor(project_interactor),
    m_physical_printer_interactor(project_interactor.physical_printer_interactor()),
    m_navigator(navigator),
    m_physical_printer_advanced_settings_dialog(advanced_dialog)
{
    m_project_interactor.removable_drive_service().add_status_listener(this);

    m_filtered_printers.set_source_model(&m_physical_printer_interactor.observable_list());
    
    auto eval_printer = [this](const PhysicalPrinterConfig& item) -> bool
    {
        using Modes = Slic3r::App::PhysicalPrinterSettingsDialog::DisplayModes;
        const auto& printer_config = m_project_interactor.preset_interactor().current_printer_config();

        switch (m_selected_mode) {
        case Modes::All: return true;
        case Modes::Compatible: return Biz::PhysicalPrinter::is_physical_printer_compatible(item, printer_config);
        default: return false;
        }
    };

    auto eval_fs = [this](const Biz::PhysicalPrinter::FileSystemExport& payload) -> bool
    {
        return payload.prefer_removable ?
            m_project_interactor.removable_drive_service().has_removable_drives() :
            true;
    };

    auto eval_connect = [this]() -> bool 
    { 
        return m_project_interactor.user_account_interactor().is_logged_in(); 
    };

    m_filtered_printers.set_filter_fn([eval_printer, eval_fs, eval_connect](const PhysicalPrinterConfig& item) {
        return std::visit([&](const auto& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, Slic3r::Biz::PhysicalPrinter::ConnectUpload>) {
                return eval_connect();
            } else if constexpr (std::is_same_v<T, Slic3r::Biz::PhysicalPrinter::PrinterUpload>) {
                return eval_printer(item); 
            } else if constexpr (std::is_same_v<T, Slic3r::Biz::PhysicalPrinter::FileSystemExport>) {
                return eval_fs(arg);
            }
            return false;
        }, item.payload);

    });

    content_item()->set_width(350);

    content()->set_padding(0);
    content()->set_orientation(Orientation::Vertical);

    m_stack_layout = content()->emplace_back<StackLayout>();
    m_stack_layout->set_orientation(Orientation::Vertical);

    create_page_list();

    m_stack_layout->set_current_index(0);
}

PhysicalPrinterSettingsDialog::~PhysicalPrinterSettingsDialog()
{
    m_project_interactor.removable_drive_service().remove_status_listener(this);
    if (m_printer_list_view) {
        m_printer_list_view->set_source_list(nullptr, true);
    }
}

void PhysicalPrinterSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

void PhysicalPrinterSettingsDialog::create_page_list()
{
    m_page_list = m_stack_layout->emplace_back<Item>();
    m_page_list->set_orientation(Orientation::Vertical);
    m_page_list->set_gap(5);
    m_page_list->set_padding(10);

    Item* keywords_row = m_page_list->emplace_back<Item>();
    keywords_row->set_padding({0, 5});

    const std::pair<DisplayModes, std::string> keywords[] = {
        // TRN Physical printer list filter button: shows every printer.
        {DisplayModes::All, _u8L("All")},
        // TRN Physical printer list filter button: shows only printers compatible with the selected printer profile.
        {DisplayModes::Compatible, _u8L("Compatible")},
    };
    m_selected_mode = keywords[0].first;

    for (const auto& [mode, label] : keywords) {
        LayoutButton* keyword_button = keywords_row->emplace_back<LayoutButton>(label);
        keyword_button->set_checkable(true);
        keyword_button->set_rounding(5);
        keyword_button->set_content_padding({10, 5});
        
        m_group_keywords.insert_button(keyword_button);
        m_button_modes[keyword_button] = mode;
    }

    m_group_keywords.callbacks().action = [this](Slic3r::App::Yoga::AbstractButton* button) {
        if (auto it = m_button_modes.find(button); it != m_button_modes.end()) {
            filter_printer_buttons(it->second);
        }
    };

    m_page_list->emplace_back<Separator>(Orientation::Horizontal);

    // Create the ViewFactory explicitly:
    auto factory = PrinterListViewFactory(
        [this](size_t index)
        {
            auto* button = static_cast<PhysicalPrinterSettingsButton*>(m_printer_list_view->get_item(index));
            m_physical_printer_interactor.select_uuid(button->uuid());
            check_printer_button(button->uuid());
            close_action();
        },
        [this](size_t index)
        {
            // cog click - select, close this dialog, open advanced dialog
             auto* button = static_cast<PhysicalPrinterSettingsButton*>(m_printer_list_view->get_item(index));
            m_physical_printer_interactor.select_uuid(button->uuid());
            check_printer_button(button->uuid());
            m_physical_printer_advanced_settings_dialog->attach_to_item(content_item(), Position::Left);
            m_navigator.set_opened_dialog(m_physical_printer_advanced_settings_dialog);
        },
        [this](size_t index)
        {
            auto& dlg_manager = App::AppServices::instance().dialog_manager();
            dlg_manager.show_yesno_dialog(
                _u8L("Remove Physical Printer?"),
                _u8L("Do you wish to remove the Physical Printer?"),
                [this, index](bool checked)
                {
                    if (checked) {
                        m_stack_layout->set_current_index(0);
                        auto* button = static_cast<PhysicalPrinterSettingsButton*>(m_printer_list_view->get_item(index));
                        m_physical_printer_interactor.remove_uuid(button->uuid());
                    }
                }
            );
        }
    );
    m_printer_list_view = m_page_list->emplace_back<PrinterListView>(std::move(factory));
    m_printer_list_view->set_flex_grow(1);
    m_printer_list_view->set_padding(Paddings(0, 0, 10, 0));
    m_printer_list_view->set_margin(Margins(0, 0, -10, 0));
    m_printer_list_view->set_gap(8);
    m_printer_list_view->set_max_height(275);
    m_printer_list_view->set_orientation(Orientation::Vertical);
    m_printer_list_view->set_source_list(&m_filtered_printers);

    check_printer_button(m_physical_printer_interactor.selected_uuid());
    set_compatibility_to_buttons();

    m_page_list->emplace_back<Separator>(Orientation::Horizontal);

    Item* row = m_page_list->emplace_back<Item>();
    row->set_justify_content(YGJustifySpaceBetween);

    LayoutButton* add_printer_button =
        m_page_list->emplace_back<LayoutButton>("Add physical printer");

    add_printer_button->callbacks().action = [this]
    {
        m_physical_printer_interactor.on_dialog_button_add_new();
        m_physical_printer_advanced_settings_dialog->attach_to_item(content_item(), Position::Left);
        m_navigator.set_opened_dialog(m_physical_printer_advanced_settings_dialog);
    };
}

void PhysicalPrinterSettingsDialog::check_printer_button(const std::string& uuid)
{
    for (size_t button_index = 0; button_index < m_printer_list_view->object_count(); ++button_index) {
        auto* button = static_cast<PhysicalPrinterSettingsButton*>(m_printer_list_view->get_item(button_index));
        ASSERT(button);
        button->set_checked(button->uuid() == uuid);
    }
}

void PhysicalPrinterSettingsDialog::on_printer_data_changed() 
{
    for (size_t button_index = 0; button_index < m_printer_list_view->object_count();
         ++button_index)
    {
        auto* button = static_cast<PhysicalPrinterSettingsButton*>(m_printer_list_view->get_item(button_index));
        ASSERT(button);
        button->update_button_text();
    }

    // A printer's data (e.g. its hardware config) may have changed its compatibility.
    set_compatibility_to_buttons();
}

void PhysicalPrinterSettingsDialog::on_selected_physical_printer_changed()
{
    check_printer_button(m_physical_printer_interactor.selected_uuid());
}

void PhysicalPrinterSettingsDialog::on_about_to_show()
{
    m_stack_layout->set_current_index(0);
}

void PhysicalPrinterSettingsDialog::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id)
    {
        m_filtered_printers.invalidate();
        set_compatibility_to_buttons();
    }
}

void PhysicalPrinterSettingsDialog::set_compatibility_to_buttons()
{
    const auto& printer_config = m_project_interactor.preset_interactor().current_printer_config();

    for (size_t button_index = 0; button_index < m_printer_list_view->object_count(); ++button_index) {
        auto* button = static_cast<PhysicalPrinterSettingsButton*>(m_printer_list_view->get_item(button_index));
        ASSERT(button);
        bool compatible = Biz::PhysicalPrinter::is_physical_printer_compatible(
            *button->state(),
            printer_config
        );
        button->set_compatible(compatible);
    }
}

void PhysicalPrinterSettingsDialog::filter_printer_buttons(DisplayModes mode)
{
    m_selected_mode = mode;

    m_filtered_printers.invalidate();
    set_compatibility_to_buttons();

    // Check if current selection is still visible, fallback to default if not
    const std::string selected_uuid = m_physical_printer_interactor.selected_uuid();
    for (size_t i = 0; i < m_filtered_printers.size(); ++i) {
        if (m_filtered_printers.at(i).uuid == selected_uuid) {
            check_printer_button(selected_uuid);
            return;
        }
    }

    m_physical_printer_interactor.select_default();
}

void PhysicalPrinterSettingsDialog::on_user_account_id_success(
    bool is_refresh,
    const std::string& username
)
{
    filter_printer_buttons(m_selected_mode);
}

void PhysicalPrinterSettingsDialog::on_user_account_logged_out()
{
    filter_printer_buttons(m_selected_mode);
}

void PhysicalPrinterSettingsDialog::on_removable_drive_status_changed(
    const boost::filesystem::path& drive_path,
    Biz::RemovableDrive::RemovableDriveStatus status
)
{
    filter_printer_buttons(m_selected_mode);
}

} // namespace Slic3r::App
