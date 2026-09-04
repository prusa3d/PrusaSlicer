#pragma once

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Navigator.hpp"

#include "Slic3r/Biz/PhysicalPrinter/IPhysicalPrinterChangedListener.hpp"
#include "Slic3r/Biz/Preset/IPresetChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include <string>

namespace Slic3r::App::Render {
class ImguiRender;
}

namespace Slic3r::Biz {
class ProjectInteractor;
}

namespace Slic3r::App {

class PhysicalPrinterSettingsDialog;
class PrinterAddDialog;
class PhysicalPrinterAdvancedSettingsDialog;
class PhysicalPrinterSettingsButton;

class SidebarActionButtons :
    public Yoga::Window,
    public Biz::PhysicalPrinter::IPhysicalPrinterChangedListener,
    public Biz::Preset::IPresetChangedListener
{
public:
    SidebarActionButtons(const std::string& name, Render::ModuleType type, Navigator* render_module_navigator);
    virtual ~SidebarActionButtons();

    virtual void on_init(Biz::ProjectInteractor* project_interactor) = 0;

    void on_printer_data_changed() override;
    void on_selected_physical_printer_changed() override;

    PhysicalPrinterSettingsDialog& physical_printer_settings_dialog();
    PhysicalPrinterAdvancedSettingsDialog& physical_printer_advanced_settings_dialog();

    void on_preset_selection_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        Biz::Preset::PresetItemType type
    ) override;
protected:

    std::unique_ptr<Yoga::LayoutButton> get_navigation_button();
    Domain::SlicingId active_bed_slicing_id() const;
    void navigate_to_other();
    void init_physical_printer_ui();
    virtual void update_buttons() {}

    /// Re-reads the selected printer into the button and refreshes its compatibility.
    void refresh_physical_printer_button();
    /// Re-evaluates the selected printer against the current printer profile.
    void update_physical_printer_compatibility();

protected:
    Biz::ProjectInteractor* m_project_interactor{nullptr};
    Navigator* m_render_module_navigator{nullptr};

    // The type of the owning render module.
    Render::ModuleType m_type{Render::ModuleType::Undef};
    // The type of the render module we need to navigate to.
    Render::ModuleType m_navigate_to_type{Render::ModuleType::Undef};

    std::string m_navigator_name;
    std::string m_navigator_tooltip;

    static constexpr float button_height{45};
    static constexpr float navig_btn_width{40.f};

    Yoga::Item* m_buttons_layout{nullptr};
    PhysicalPrinterSettingsButton* m_physical_printer_button{nullptr};
    PrinterAddDialog* m_printer_add_dialog{nullptr};
    PhysicalPrinterSettingsDialog* m_physical_printer_settings_dialog{nullptr};
    PhysicalPrinterAdvancedSettingsDialog* m_physical_printer_advanced_settings_dialog{nullptr};
};

} // namespace Slic3r::App
