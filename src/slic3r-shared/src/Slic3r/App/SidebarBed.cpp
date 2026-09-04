#include "Slic3r/App/SidebarBed.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/PrinterSettingsButton.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/LogicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/PhysicalPrinterSettingsDialog.hpp"
#include "Slic3r/App/MaterialSelectionDialog.hpp"
#include "Slic3r/App/MaterialSettingsDialog.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <imgui/imgui_internal.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SidebarBed::SidebarBed(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Window("SidebarBed"),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    m_printer_add_dialog              = emplace_back<PrinterAddDialog>(m_project_interactor);
    m_logical_printer_settings_dialog = emplace_back<LogicalPrinterSettingsDialog>(
        project_interactor,
        m_printer_add_dialog,
        m_navigator
    );

    m_material_selection_dialog =
        emplace_back<MaterialSelectionDialog>(project_interactor, m_navigator);

    set_orientation(Orientation::Vertical);
    set_gap(15_fpx);
    set_min_width(220);
    set_flex_shrink(0.f);

    m_material_button_group = std::make_shared<ButtonGroup>();
    m_material_button_group->set_always_checked(false);

    m_bed_name = emplace_back<Text>("Unkown");
    m_bed_name->set_margin(Paddings(0.f, -5.f, 0.f, 0.f));
    m_bed_name->set_font_type(Render::ImguiFontType::Bold);
    m_bed_name->set_font_size(1.15_rem);

    m_logical_printer_button = emplace_back<PrinterSettingsButton>();
    m_logical_printer_button->set_flex_grow(1.f);
    m_logical_printer_button->set_visible_cog(true);

    m_logical_printer_settings_dialog
        ->attach_to_item(m_logical_printer_button, Position::Left, 40_fpx, AlignU::Start);
    m_logical_printer_settings_dialog->callbacks().opened = [this]()
    { m_logical_printer_button->set_checked(true); };
    m_logical_printer_settings_dialog->callbacks().closed = [this]()
    { m_logical_printer_button->set_checked(false); };

    auto toggle_logical_printer_settings_dialog = [this]()
    {
        if (m_logical_printer_settings_dialog->opened()) {
            m_navigator.set_opened_dialog(nullptr);
        } else {
            m_navigator.set_opened_dialog(m_logical_printer_settings_dialog);
        }
    };
    m_logical_printer_button->callbacks().action = [toggle_logical_printer_settings_dialog]()
    { toggle_logical_printer_settings_dialog(); };
    m_logical_printer_button->on_cog() = [toggle_logical_printer_settings_dialog, this]()
    {
        toggle_logical_printer_settings_dialog();
        m_logical_printer_settings_dialog->select_page_settings();
    };

    m_list_view = emplace_back<MaterialListView>(MaterialListViewFactory{
        std::weak_ptr<ButtonGroup>(m_material_button_group),
        [this](size_t index)
        {
            m_material_selection_dialog->set_material_index(index);
            m_material_selection_dialog->material_settings_dialog().set_current_tab(index);
            if (!m_material_selection_dialog->material_settings_dialog().opened()) {
                m_navigator.set_opened_dialog(
                    &m_material_selection_dialog->material_settings_dialog()
                );
            }
        },
        [toggle_logical_printer_settings_dialog, this](size_t index)
        {
            toggle_logical_printer_settings_dialog();
            m_logical_printer_settings_dialog->select_page_settings();
        },
        m_project_interactor
    });
    m_list_view->set_source_list(&m_project_interactor.preset_interactor().material_presets());
    m_list_view->set_orientation(Orientation::Vertical);
    m_list_view->set_gap(5_fpx);

    m_material_selection_dialog->attach_to_item(m_list_view, Position::Left, 40_fpx, AlignU::Start);
    m_material_selection_dialog->callbacks().closed = [this]()
    {
        for (AbstractButton* button : m_material_button_group->buttons()) {
            button->set_checked(false);
        }
    };

    m_material_selection_dialog->material_selection_callbacks().advanced_settings_tab_opened =
        [this](size_t current_index)
    {
        if (m_material_selection_dialog->opened()) {
            dynamic_cast<AbstractButton*>(m_list_view->get_item(current_index))->set_checked(true);
            m_material_selection_dialog->set_material_index(current_index);
        }
    };

    m_material_button_group->callbacks().action = [this](AbstractButton* action_button)
    {
        if (action_button->checked()) {
            m_material_selection_dialog->set_material_index(
                m_list_view->index_of(action_button).value()
            );
            m_material_selection_dialog->material_settings_dialog().set_current_tab(
                m_list_view->index_of(action_button).value()
            );
            if (!m_material_selection_dialog->opened()) {
                m_navigator.set_opened_dialog(m_material_selection_dialog);
            }
        } else {
            m_navigator.set_opened_dialog(nullptr);
        }
    };

    m_project_interactor.preset_interactor()
        .printer_presets()
        .add_listener<Biz::IListSelectionChangedListener>(this);
    on_list_selection_changed(
        m_project_interactor.preset_interactor().printer_presets().selected_index()
    );

    m_project_interactor.scene_interactor().add_listener<Biz::ISelectedBedInstancesChangedListener>(
        this
    );
    on_selected_bed_instances_changed(
        m_project_interactor.selected_project_id(),
        m_project_interactor.scene_interactor().bed_selection()
    );
}

void SidebarBed::on_list_selection_changed(Domain::SelectionId new_selection)
{
    if (new_selection == Domain::INVALID_ID) {
        return;
    }

    const auto& preset_interactor = m_project_interactor.preset_interactor();
    const Biz::Preset::PresetItem& preset_item =
        preset_interactor.printer_presets().items().at(new_selection);

    const bool is_modified_preset = preset_interactor.is_printer_preset_selected_and_dirty(
        preset_item.hw_printer_config_id,
        preset_item.id
    );

    m_selected_printer_preset_name = preset_item.ui_preset_name();
    m_logical_printer_button->set_printer_name(m_selected_printer_preset_name, is_modified_preset);
    m_logical_printer_button->set_preset_name(preset_item.ui_hw_config_name());
    m_logical_printer_button->set_tooltip(
        preset_item.ui_preset_name() + "\n" + preset_item.ui_hw_config_name()
    );

    const Domain::Preset::HwPrinterConfig& printer_config =
        preset_interactor.current_printer_config();

    if (printer_config.visual.thumbnail.has_value()) {
        const std::string image_path =
            printer_config.relative_path_to_assets() + printer_config.visual.thumbnail.value();

        m_logical_printer_button->set_image(image_path);
    }
}

void SidebarBed::on_selected_bed_instances_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::BedSelection& bed_selection
)
{
    if (project_id == Domain::INVALID_ID
        || m_project_interactor.selected_project_id() != project_id)
    {
        return;
    }

    Domain::BedInstance* bed_instance =
        m_project_interactor.selected_project().find_bed_instance_by_id(
            bed_selection.last_selected_bed().instance_id
        );

    ASSERT(bed_instance);

    m_bed_name->set_text(bed_instance->name());
}

void SidebarBed::refresh_printer_label_color()
{
    bool is_modified_preset =
        m_project_interactor.preset_interactor().printer_cbi().config_box_list().lock()->is_dirty();
    m_logical_printer_button->set_printer_name(m_selected_printer_preset_name, is_modified_preset);
}

void SidebarBed::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{ std::get<Domain::FDMConfigLocation>(item.location()) };
        if (location != Domain::FDMConfigLocation::Printer) {
            return;
        }
    }
    else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{ std::get<Domain::SLAConfigLocation>(item.location()) };
        if (location != Domain::SLAConfigLocation::Printer) {
            return;
        }
    }

    refresh_printer_label_color();
}

void SidebarBed::on_preset_bundles_loaded()
{
    refresh_printer_label_color();
}

LogicalPrinterSettingsDialog& SidebarBed::logical_printer_settings_dialog()
{
    return *m_logical_printer_settings_dialog;
}

PrinterAddDialog& SidebarBed::printer_add_dialog()
{
    return *m_printer_add_dialog;
}

MaterialSelectionDialog& SidebarBed::material_selection_dialog()
{
    return *m_material_selection_dialog;
}

} // namespace Slic3r::App
