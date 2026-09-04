#pragma once

#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/Yoga/Dialog.hpp"
#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/LogicalPrinterSettingsButton.hpp"
#include "Slic3r/App/PrinterNozzleRow.hpp"
#include "Slic3r/App/IAppConfigChangedListener.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"
#include "Slic3r/Biz/ObservableListSearcher.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class InputText;
class StackLayout;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::ColorMix {
class ColorMixDialog;
} // namespace Slic3r::App::ColorMix

namespace Slic3r::App {
class PrinterAddDialog;
class Navigator;
class PrinterAdvancedSettingsDialog;
class WarningPanel;

class LogicalPrinterSettingsDialog :
    public Yoga::Dialog,
    public Biz::Preset::IPresetChangedListener,
    public Biz::IListSelectionChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public IAppConfigChangedListener
{
public:
    LogicalPrinterSettingsDialog(
        Biz::ProjectInteractor& project_interactor,
        PrinterAddDialog* printer_add_dialog,
        Navigator& navigator
    );

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;

    void on_hw_item_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::HwItemType type
    ) override;

    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    void on_selected_project_changed_final(size_t index) override;

    PrinterAdvancedSettingsDialog& printer_advanced_settings_dialog();
    ColorMix::ColorMixDialog& color_mix_dialog();

    void select_page_settings();

    void on_app_config_changed(const std::string& key) override;

private:
    void create_page_list();
    void create_page_settings();

    void on_about_to_show() override;
    void update_settings_data();
    void update_color_mix_visibility();

    void on_config_container_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

protected:
    void close_action() override;

    void update_warning();

private:
    using PrinterListViewFactory = Yoga::ViewFactory<
        LogicalPrinterSettingsButton,
        Biz::Preset::PresetItem,
        LogicalPrinterSettingsButton::FnIndexClicked,
        LogicalPrinterSettingsButton::FnIndexClicked,
        LogicalPrinterSettingsButton::FnIndexClicked,
        const Biz::Preset::PresetInteractor&>;
    using PrinterListView = Yoga::ListView<
        LogicalPrinterSettingsButton,
        Biz::Preset::PresetItem,
        PrinterListViewFactory,
        Yoga::ScrollArea>;

    using NozzleListView = Yoga::ListView<
        PrinterNozzleRow,
        Biz::Preset::ToolConfigItemObservableList,
        Yoga::ViewFactory<
            PrinterNozzleRow,
            Biz::Preset::ToolConfigItemObservableList,
            Biz::Preset::PresetInteractor&,
            std::function<void(bool)>>>;

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        LogicalPrinterSettingsDialog>
        m_preset_changed_listener_scope;

    Biz::ListenerScope<
        Biz::IListSelectionChangedListener,
        Biz::Preset::PresetItemObservableList,
        LogicalPrinterSettingsDialog>
        m_preset_list_selection_changed_listener_scope;

    Biz::ListenerScope<
        Biz::ISelectedProjectChangedListener,
        Biz::ProjectInteractor,
        LogicalPrinterSettingsDialog>
        m_selected_project_changed_listener_scope;

    Biz::ListenerScope<IAppConfigChangedListener, AppConfigInteractor, LogicalPrinterSettingsDialog>
        m_app_config_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    PrinterListView* m_printer_list_view{nullptr};
    Yoga::StackLayout* m_stack_layout{nullptr};

    // PageList attributes
    Yoga::Item* m_page_list{nullptr};
    Yoga::InputText* m_input_text_search{nullptr};
    Yoga::LayoutButton* m_only_favorites_button{nullptr};
    Yoga::LayoutButton* m_add_printer_button{nullptr};
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::Preset::PresetItem>>
        m_preset_favorite_filter;
    Biz::UnsharedPointer<Biz::ObservableListSearcher<Biz::Preset::PresetItem>> m_preset_searcher;

    // Page settings attributes
    Yoga::Item* m_page_settings{nullptr};
    Yoga::Text* m_text_printer_name{nullptr};
    Yoga::Icon* m_printer_icon{nullptr};
    Yoga::ComboBoxListViewSelection<Domain::Preset::HwSheetConfigDef>* m_combo_sheets;
    WarningPanel* m_warning{nullptr};
    NozzleListView* m_nozzle_list_view{nullptr};
    Yoga::LayoutButton* m_color_mix_button{nullptr};

    PrinterAdvancedSettingsDialog* m_advanced_dialog{nullptr};
    PrinterAddDialog* m_printer_add_dialog{nullptr};
    ColorMix::ColorMixDialog* m_color_mix_dialog{nullptr};
};

} // namespace Slic3r::App
