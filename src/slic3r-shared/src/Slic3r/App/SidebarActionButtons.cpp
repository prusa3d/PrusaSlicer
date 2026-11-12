#include "Slic3r/App/SidebarActionButtons.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsButton.hpp"
#include "Slic3r/App/ExportDestinationButton.hpp"

#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/PhysicalPrinter/PhysicalPrinterInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <imgui/imgui_internal.h>

using namespace Slic3r::Biz::PhysicalPrinter;

namespace Slic3r::App {

using RMType = Render::ModuleType;

SidebarActionButtons::SidebarActionButtons(
    const std::string& name,
    Render::ModuleType type,
    Navigator* render_module_navigator
) :
    Yoga::Window(name),
    m_render_module_navigator(render_module_navigator),
    m_type(type)
{
    set_transparent(true);
    ASSERT(type != RMType::Undef);

    m_navigator_name = m_type == RMType::Plater ? ">" : "<";
    m_navigator_tooltip =
        m_type == RMType::Plater ? Biz::_u8L("Show Preview") : Biz::_u8L("Back to Plater");
    m_navigate_to_type = m_type == RMType::Plater ? RMType::Preview : RMType::Plater;

    set_min_width(220);
    set_flex_shrink(0);
}

SidebarActionButtons::~SidebarActionButtons()
{
    if (m_project_interactor) {
        m_project_interactor->physical_printer_interactor()
            .remove_listener<Biz::PhysicalPrinter::IPhysicalPrinterChangedListener>(this);
        m_project_interactor->export_destination_interactor()
            .remove_listener<Biz::ExportDestination::IExportDestinationChangedListener>(this);
        m_project_interactor->preset_interactor()
            .remove_listener<Biz::Preset::IPresetChangedListener>(this);
    }
}

void SidebarActionButtons::init_physical_printer_ui()
{
    m_project_interactor->physical_printer_interactor()
        .add_listener<Biz::PhysicalPrinter::IPhysicalPrinterChangedListener>(this);
    m_project_interactor->export_destination_interactor()
        .add_listener<Biz::ExportDestination::IExportDestinationChangedListener>(this);
    m_project_interactor->preset_interactor().add_listener<Biz::Preset::IPresetChangedListener>(
        this
    );

    m_buttons_layout = emplace_back<Yoga::Item>();
    m_buttons_layout->set_orientation(Yoga::Orientation::Vertical);
    m_buttons_layout->set_gap(10.0f);
    m_buttons_layout->set_flex_grow(1);

    // TRN Sidebar label above the export destination selection.
    m_buttons_layout->emplace_back<Yoga::Text>(Biz::_u8L("Export to"));

    m_export_destination_button = m_buttons_layout->emplace_back<ExportDestinationButton>(
        m_project_interactor->export_destination_interactor()
    );
    m_export_destination_button->set_self_align(YGAlignStretch);
}

std::unique_ptr<Yoga::LayoutButton> SidebarActionButtons::get_navigation_button()
{
    auto result{std::make_unique<Yoga::LayoutButton>(
        m_navigator_name,
        Render::Icon::None,
        m_navigator_tooltip
    )};
    result->set_background_color(m_theme->color_imgui(Platform::Color::AccentSecondary));
    result->set_label_font_type(Render::ImguiFontType::Bold);
    result->set_min_width(navig_btn_width);
    result->set_min_height(button_height);

    result->callbacks().action = [this]() { navigate_to_other(); };
    return result;
}

Domain::SlicingId SidebarActionButtons::active_bed_slicing_id() const
{
    return {
        m_project_interactor->selected_project_id(),
        m_project_interactor->scene_interactor().bed_selection().last_selected_bed().instance_id
    };
}

void SidebarActionButtons::navigate_to_other()
{
    m_render_module_navigator->navigate_to_module_type(m_navigate_to_type);
}

void SidebarActionButtons::refresh_physical_printer_button()
{
    update_buttons();
}

void SidebarActionButtons::on_selected_physical_printer_changed()
{
    refresh_physical_printer_button();
}

void SidebarActionButtons::on_export_destination_changed()
{
    update_buttons();
}

void SidebarActionButtons::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{}

} // namespace Slic3r::App
