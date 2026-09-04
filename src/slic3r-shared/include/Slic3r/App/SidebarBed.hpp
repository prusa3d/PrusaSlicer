#pragma once

#include "Slic3r/App/Yoga/Window.hpp"

#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/MaterialSettingsButton.hpp"

#include "Slic3r/Biz/ObservableListWithSelection.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/Biz/Platform/ListenerScope.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class Navigator;
class LogicalPrinterSettingsDialog;
class PrinterAddDialog;
class MaterialSelectionDialog;
class MaterialListView;

namespace Yoga {
class Text;
class AbstractButton;
class PrinterSettingsButton;
} // namespace Yoga

class SidebarBed :
    public Yoga::Window,
    public Biz::IListSelectionChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::Preset::IPresetChangedListener
{
public:
    explicit SidebarBed(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;
    void on_preset_bundles_loaded() override;

    LogicalPrinterSettingsDialog& logical_printer_settings_dialog();
    PrinterAddDialog& printer_add_dialog();
    MaterialSelectionDialog& material_selection_dialog();

private:
    void refresh_printer_label_color();

private:
    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SidebarBed>
        m_preset_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::Text* m_bed_name{nullptr};
    Yoga::PrinterSettingsButton* m_logical_printer_button{nullptr};

    std::shared_ptr<Yoga::ButtonGroup> m_material_button_group;
    using MaterialListViewFactory = Yoga::ViewFactory<
        Yoga::MaterialSettingsButton,
        Biz::Preset::PresetItemObservableList,
        std::weak_ptr<Yoga::ButtonGroup>,
        Yoga::MaterialSettingsButton::FnIndexClicked,
        Yoga::MaterialSettingsButton::FnIndexClicked,
        Biz::ProjectInteractor&>;
    using MaterialListView = Yoga::ListView<
        Yoga::MaterialSettingsButton,
        Biz::Preset::PresetItemObservableList,
        MaterialListViewFactory>;

    MaterialListView* m_list_view{nullptr};
    LogicalPrinterSettingsDialog* m_logical_printer_settings_dialog{nullptr};
    PrinterAddDialog* m_printer_add_dialog{nullptr};
    MaterialSelectionDialog* m_material_selection_dialog{nullptr};

    std::string m_selected_printer_preset_name{};

};

} // namespace Slic3r::App
