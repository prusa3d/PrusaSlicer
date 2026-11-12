#pragma once

#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/LogicalPrinterSettingsButton.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsButton.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ObservableListSearcher.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::PhysicalPrinter {
struct PhysicalPrinterConfig;
} // namespace Slic3r::Biz::PhysicalPrinter

namespace Slic3r::App::Yoga {
class InputText;
class StackLayout;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {
class PrinterAddDialog;
class Navigator;
class WarningPanel;
class PhysicalPrinterForm;

class PrinterSettingsDialog :
    public Yoga::Dialog,
    public Biz::Preset::IPresetChangedListener,
    public Biz::ISelectedProjectChangedListener
{
public:
    PrinterSettingsDialog(
        Biz::ProjectInteractor& project_interactor,
        PrinterAddDialog* printer_add_dialog,
        Navigator& navigator
    );

    void on_selected_project_changed_final(size_t index) override;

    void select_page_settings(const Biz::PhysicalPrinter::PhysicalPrinterConfig& printer);

private:
    void create_page_list();
    void create_page_settings();

    void on_about_to_show() override;
    void update_settings_data(const Biz::PhysicalPrinter::PhysicalPrinterConfig& printer);

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;
    void on_preset_bundles_reloaded() override;

protected:
    void close_action() override;

    void update_warning();

private:
    /// Order matches the filter buttons, value is stored in AppConfig
    enum class PrinterFilter { All, Local, Connect };

    using PrinterListViewFactory = Yoga::ViewFactory<
        PhysicalPrinterSettingsButton,
        Biz::PhysicalPrinter::PhysicalPrinterConfig,
        PhysicalPrinterSettingsButton::FnIndexClicked,
        PhysicalPrinterSettingsButton::FnIndexClicked,
        PhysicalPrinterSettingsButton::FnIndexClicked,
        const Biz::Preset::PresetInteractor&,
        Biz::PhysicalPrinter::PhysicalPrinterInteractor&>;

    using PrinterListView = Yoga::ListView<
        PhysicalPrinterSettingsButton,
        Biz::PhysicalPrinter::PhysicalPrinterConfig,
        PrinterListViewFactory,
        Yoga::ScrollArea>;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        PrinterSettingsDialog>
        m_preset_changed_listener_scope;

    Biz::ListenerScope<
        Biz::ISelectedProjectChangedListener,
        Biz::ProjectInteractor,
        PrinterSettingsDialog>
        m_selected_project_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    PrinterListView* m_printer_list_view{nullptr};
    Yoga::StackLayout* m_stack_layout{nullptr};

    // PageList attributes
    Yoga::Item* m_page_list{nullptr};
    Yoga::InputText* m_input_text_search{nullptr};
    Yoga::LayoutButton* m_add_printer_button{nullptr};
    Biz::UnsharedPointer<Biz::ObservableListSearcher<Biz::PhysicalPrinter::PhysicalPrinterConfig>>
        m_preset_searcher;
    Biz::UnsharedPointer<
        Biz::ObservableListSortFilter<Biz::PhysicalPrinter::PhysicalPrinterConfig>>
        m_printer_filter;
    Yoga::ButtonGroup m_printer_filter_button_group;
    std::vector<Yoga::LayoutButton*> m_printer_filter_buttons;
    PrinterFilter m_printer_filter_type{PrinterFilter::All};

    // Page settings attributes
    Yoga::Item* m_page_settings{nullptr};
    Yoga::Text* m_text_printer_name{nullptr};
    Yoga::Icon* m_printer_icon{nullptr};
    PhysicalPrinterForm* m_physical_printer_form{nullptr};
    WarningPanel* m_warning{nullptr};
    PrinterAddDialog* m_printer_add_dialog{nullptr};
};

} // namespace Slic3r::App
