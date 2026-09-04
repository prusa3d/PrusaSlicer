#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsButton.hpp"
#include "Slic3r/Biz/ObservableList.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"

#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterConfig.hpp"
#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/App/ConfigSettingsDialog.hpp"
#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/UserAccount/IUserAccountListener.hpp"
#include "Slic3r/Biz/UserAccount/UserAccountInteractor.hpp"
#include "Slic3r/Biz/RemovableDrive/IRemovableDriveStatusListener.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
class ConfigBoxInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::PhysicalPrinter {
class PhysicalPrinterInteractor;
} // namespace Slic3r::Biz::PhysicalPrinter

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::Yoga {
class StackLayout;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;

class PrinterAddDialog;
class PhysicalPrinterAdvancedSettingsDialog;

class PhysicalPrinterSettingsDialog :
    public Yoga::Dialog,
    public Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::UserAccount::IUserAccountListener,
    public Biz::RemovableDrive::IRemovableDriveStatusListener
{
public:
    PhysicalPrinterSettingsDialog(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        PhysicalPrinterAdvancedSettingsDialog* advanced_dialog
    );
    ~PhysicalPrinterSettingsDialog();

    void on_printer_data_changed() override;
    void on_selected_physical_printer_changed() override;

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_user_account_id_success(bool is_refresh, const std::string& username) override;
    void on_user_account_logged_out() override;

    void on_removable_drive_status_changed(
        const boost::filesystem::path& drive_path,
        Biz::RemovableDrive::RemovableDriveStatus status
    ) override;

protected:
    void close_action() override;

private:
    enum class DisplayModes {
        Compatible, All
    };

private:
    void create_page_list();

    void check_printer_button(const std::string& uuid);

    void on_about_to_show() override;
     
    void filter_printer_buttons(DisplayModes mode);

    void set_compatibility_to_buttons();

private:

    using PrinterListViewFactory = Yoga::ViewFactory<
        PhysicalPrinterSettingsButton,
        Biz::PhysicalPrinter::PhysicalPrinterConfig,
        PhysicalPrinterSettingsButton::FnIndexClicked,
        PhysicalPrinterSettingsButton::FnIndexClicked,
        PhysicalPrinterSettingsButton::FnIndexClicked>;

    using PrinterListView = Yoga::ListView<
        PhysicalPrinterSettingsButton,
        Biz::PhysicalPrinter::PhysicalPrinterConfig,
        PrinterListViewFactory,
        Yoga::ScrollArea>;

    Biz::ListenerScope<
        Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
        Biz::PhysicalPrinter::PhysicalPrinterInteractor,
        PhysicalPrinterSettingsDialog>
        m_physical_printer_changed_listener_scope;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        PhysicalPrinterSettingsDialog>
        m_preset_changed_listener_scope;
    
    Biz::ListenerScope<
        Biz::UserAccount::IUserAccountListener,
        Biz::UserAccount::UserAccountInteractor,
        PhysicalPrinterSettingsDialog>
        m_user_account_listener_scope;
        
    Biz::ProjectInteractor& m_project_interactor;
    Biz::PhysicalPrinter::PhysicalPrinterInteractor& m_physical_printer_interactor;
    Navigator& m_navigator;

    Biz::ObservableListSortFilter<Biz::PhysicalPrinter::PhysicalPrinterConfig> m_filtered_printers;
    PrinterListView* m_printer_list_view{nullptr};
    PhysicalPrinterAdvancedSettingsDialog* m_physical_printer_advanced_settings_dialog{nullptr};
    Yoga::StackLayout* m_stack_layout{nullptr};
    Yoga::Item* m_page_list{nullptr};
    Yoga::Item* m_page_settings{nullptr};
    Yoga::ButtonGroup m_group_keywords;
    std::unordered_map<Slic3r::App::Yoga::AbstractButton*, DisplayModes> m_button_modes;
    DisplayModes m_selected_mode{DisplayModes::All};
};
} // namespace Slic3r::App
