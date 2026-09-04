#include "Slic3r/App/SidebarActionButtons.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterAdvancedSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsButton.hpp"

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
    ASSERT(type != RMType::Undef);

    m_navigator_name    = m_type == RMType::Plater ? ">" : "<";
    m_navigator_tooltip = m_type == RMType::Plater ? "Show Preview" : "Back to Plater";
    m_navigate_to_type  = m_type == RMType::Plater ? RMType::Preview : RMType::Plater;

    set_min_width(220);
    set_flex_shrink(0);
}

SidebarActionButtons::~SidebarActionButtons() {
    if (m_project_interactor) {
        m_project_interactor->physical_printer_interactor()
            .remove_listener<Biz::PhysicalPrinter::IPhysicalPrinterChangedListener>(this);
        m_project_interactor->preset_interactor().remove_listener<Biz::Preset::IPresetChangedListener>(this);
    }
}

void SidebarActionButtons::init_physical_printer_ui()
{
    m_project_interactor->physical_printer_interactor()
        .add_listener<Biz::PhysicalPrinter::IPhysicalPrinterChangedListener>(this);
    m_project_interactor->preset_interactor().add_listener<Biz::Preset::IPresetChangedListener>(this);
    
    m_physical_printer_advanced_settings_dialog = emplace_back<PhysicalPrinterAdvancedSettingsDialog>(
        *m_project_interactor,
        *m_render_module_navigator
    );
    
    m_physical_printer_settings_dialog = emplace_back<PhysicalPrinterSettingsDialog>(
        *m_project_interactor,
        *m_render_module_navigator,
        m_physical_printer_advanced_settings_dialog
    );

    m_physical_printer_advanced_settings_dialog->dialog_callbacks().close_requested = [this]()
    { m_render_module_navigator->set_opened_dialog(m_physical_printer_settings_dialog); };

    auto& interactor = m_project_interactor->physical_printer_interactor();

    m_buttons_layout = emplace_back<Yoga::Item>();
    m_buttons_layout->set_orientation(Yoga::Orientation::Vertical);
    m_buttons_layout->set_gap(10.0f);
    m_buttons_layout->set_flex_grow(1);

    m_buttons_layout->emplace_back<Yoga::Text>(Biz::_u8L("Select Destination"));

    m_physical_printer_button = m_buttons_layout->emplace_back<PhysicalPrinterSettingsButton>(
        0,
        interactor.selected_physical_printer_data(),
        [](size_t) {},
        [](size_t) {},
        [](size_t) {}
    );

    m_physical_printer_button->set_bin_supported(false);
    m_physical_printer_button->set_visible(true);
    m_physical_printer_button->set_self_align(YGAlignStretch);
    m_physical_printer_button->set_flex_grow(1.f);

    m_physical_printer_settings_dialog->attach_to_item(this, Yoga::Position::Left);

    m_physical_printer_settings_dialog->callbacks().opened = [this]() {
        m_physical_printer_button->set_checked(true);
    };
    
    m_physical_printer_settings_dialog->callbacks().closed = [this]() {
        m_physical_printer_button->set_checked(false);
    };

    m_physical_printer_button->callbacks().action = [this]() {
        m_render_module_navigator->set_opened_dialog(
            m_physical_printer_settings_dialog->opened() ? nullptr : m_physical_printer_settings_dialog
        );
    };
    m_physical_printer_button->set_visible_expand_icon(true);

    m_physical_printer_button->on_cog() = [this]() {
        if (m_physical_printer_advanced_settings_dialog->opened()) {
            m_render_module_navigator->set_opened_dialog(nullptr);
        } else {
            m_physical_printer_advanced_settings_dialog->attach_to_item(
                m_physical_printer_settings_dialog->content_item(),
                Yoga::Position::Left
            );
            m_render_module_navigator->set_opened_dialog(m_physical_printer_advanced_settings_dialog);
        }
    };

    update_physical_printer_compatibility();
}

std::unique_ptr<Yoga::LayoutButton> SidebarActionButtons::get_navigation_button()
{
    auto result{
        std::make_unique<Yoga::LayoutButton>(m_navigator_name, Render::Icon::None, m_navigator_tooltip)
    };
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

PhysicalPrinterSettingsDialog& SidebarActionButtons::physical_printer_settings_dialog()
{
    return *m_physical_printer_settings_dialog;
}

PhysicalPrinterAdvancedSettingsDialog& SidebarActionButtons::physical_printer_advanced_settings_dialog()
{
    return *m_physical_printer_advanced_settings_dialog;
}

void SidebarActionButtons::refresh_physical_printer_button()
{
    if (!m_project_interactor) {
        return;
    }
    PhysicalPrinterInteractor& physical_printer_interactor =
        m_project_interactor->physical_printer_interactor();
    const Biz::PhysicalPrinter::PhysicalPrinterConfig& physical_printer =
        physical_printer_interactor.selected_physical_printer_data();

    m_physical_printer_button->set_state(physical_printer);

    update_buttons();

    // The printer's data (e.g. its hardware config) may have changed its compatibility.
    update_physical_printer_compatibility();
}

void SidebarActionButtons::update_physical_printer_compatibility()
{
    const auto& printer_config = m_project_interactor->preset_interactor().current_printer_config();
    bool compatible = Biz::PhysicalPrinter::is_physical_printer_compatible(
        *m_physical_printer_button->state(),
        printer_config
    );
    m_physical_printer_button->set_compatible(compatible);
}

void SidebarActionButtons::on_printer_data_changed()
{
    refresh_physical_printer_button();
}

void SidebarActionButtons::on_selected_physical_printer_changed()
{
    refresh_physical_printer_button();
}

void SidebarActionButtons::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (m_project_interactor->selected_project_id() == project_id
        && m_project_interactor->selected_config_container_id() == config_container_id)
    {
        update_physical_printer_compatibility();
    }
}

} // namespace Slic3r::App
