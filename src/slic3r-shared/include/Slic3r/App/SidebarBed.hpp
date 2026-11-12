#pragma once

#include "Slic3r/App/Yoga/Window.hpp"

#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Yoga/MaterialSettingsButton.hpp"
#include "Slic3r/App/Yoga/ComboBoxListViewSelection.hpp"

#include "Slic3r/Biz/ObservableListWithSelection.hpp"
#include "Slic3r/Biz/ISelectedBedInstanceChangedListener.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"

#include "Slic3r/Biz/Platform/ListenerScope.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::ColorMix {
class ColorMixDialog;
} // namespace Slic3r::App::ColorMix

namespace Slic3r::App {

class Navigator;
class PrinterAdvancedSettingsDialog;
class PrinterSettingsDialog;
class PrinterAddDialog;
class MaterialSelectionDialog;
class MaterialListView;
class PrinterSettingsButton;

namespace Yoga {
class Text;
class AbstractButton;
} // namespace Yoga

class SidebarBed :
    public Yoga::Window,
    public Biz::IListSelectionChangedListener,
    public Biz::ISelectedBedInstancesChangedListener,
    public Biz::Preset::IPresetChangedListener,
    public Biz::PhysicalPrinter::IPhysicalPrinterChangedListener
{
public:
    explicit SidebarBed(Biz::ProjectInteractor& project_interactor, Navigator& navigator);

    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    // on_cc_change
    // this->update_color_mix_visibility();

    void on_selected_bed_instances_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::BedSelection& bed_selection
    ) override;

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override;
    void on_hw_item_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::HwItemType type
    ) override;
    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;
    void on_preset_bundles_reloaded() override;

    void on_selected_physical_printer_changed() override;
    void on_selected_physical_printer_updated() override;

    PrinterSettingsDialog& logical_printer_settings_dialog();
    PrinterAddDialog& printer_add_dialog();
    MaterialSelectionDialog& material_selection_dialog();
    ColorMix::ColorMixDialog& color_mix_dialog();
    PrinterAdvancedSettingsDialog& printer_advanced_settings_dialog();

private:
    void refresh_printer_label_color();
    void update_color_mix_visibility();
    void update_purging_volumes_button_visibility();
    void update_printer_button();

private:
    using PresetComboBox = Yoga::ComboBoxListViewSelection<Biz::Preset::PresetItem>;

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

    Biz::ListenerScope<
        Biz::Preset::IPresetChangedListener,
        Biz::Preset::PresetInteractor,
        SidebarBed>
        m_preset_changed_listener_scope;
    Biz::ListenerScope<
        Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
        Biz::PhysicalPrinter::PhysicalPrinterInteractor,
        SidebarBed>
        m_physical_printer_changed_listener_scope;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;

    Yoga::Text* m_bed_name{nullptr};
    PrinterSettingsButton* m_logical_printer_button{nullptr};
    PresetComboBox* m_preset_combo_box{nullptr};

    std::shared_ptr<Yoga::ButtonGroup> m_material_button_group;
    Yoga::LayoutButton* m_purging_volumes_button{nullptr};

    MaterialListView* m_list_view{nullptr};
    PrinterSettingsDialog* m_printer_settings_dialog{nullptr};
    PrinterAdvancedSettingsDialog* m_advanced_dialog{nullptr};
    PrinterAddDialog* m_printer_add_dialog{nullptr};
    MaterialSelectionDialog* m_material_selection_dialog{nullptr};

    Yoga::LayoutButton* m_color_mix_button{nullptr};
    ColorMix::ColorMixDialog* m_color_mix_dialog{nullptr};

    std::string m_selected_printer_preset_name{};
};

} // namespace Slic3r::App
