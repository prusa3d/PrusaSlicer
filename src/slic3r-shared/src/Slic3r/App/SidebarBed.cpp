#include "Slic3r/App/SidebarBed.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/PrinterSettingsButton.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Material/MaterialSelectionDialog.hpp"
#include "Slic3r/App/Material/MaterialSettingsDialog.hpp"
#include "Slic3r/App/PrinterAddDialog.hpp"
#include "Slic3r/App/PrinterSettingsDialog.hpp"
#include "Slic3r/App/ColorMix/ColorMixDialog.hpp"
#include "Slic3r/App/PrinterAdvancedSettingsDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Preset/PresetSelectionCheck.hpp"

#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"

#include <imgui/imgui_internal.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SidebarBed::SidebarBed(Biz::ProjectInteractor& project_interactor, Navigator& navigator) :
    Window("SidebarBed"),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_physical_printer_changed_listener_scope(project_interactor.physical_printer_interactor(), *this),
    m_project_interactor(project_interactor),
    m_navigator(navigator)
{
    set_transparent(true);
    m_printer_add_dialog = emplace_back<PrinterAddDialog>(m_project_interactor, m_navigator);
    m_printer_settings_dialog =
        emplace_back<PrinterSettingsDialog>(project_interactor, m_printer_add_dialog, m_navigator);

    m_advanced_dialog =
        emplace_back<PrinterAdvancedSettingsDialog>(project_interactor, m_navigator);
    m_advanced_dialog->attach_to_item(this, Position::Left);

    m_color_mix_dialog = emplace_back<ColorMix::ColorMixDialog>(project_interactor, m_navigator);

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

    Item* preset_row = emplace_back<Item>();
    preset_row->set_gap(5_fpx);

    Text* preset_label = preset_row->emplace_back<Text>(Biz::_u8L("Preset"));
    preset_label->set_align({AlignH::Left, AlignV::Center});
    m_preset_combo_box = preset_row->emplace_back<PresetComboBox>();
    m_preset_combo_box->set_object_name("PresetComboBox");
    m_preset_combo_box->set_flex_grow(1);
    m_preset_combo_box->set_get_name_fn([](const Biz::Preset::PresetItem* preset_item)
                                        { return preset_item->ui_preset_name(); });
    m_preset_combo_box->set_source_list(
        &m_project_interactor.preset_interactor().printer_presets()
    );
    m_preset_combo_box->callbacks().selection_changed = [this](int index)
    {
        const Biz::Preset::PresetItem& preset =
            m_project_interactor.preset_interactor().printer_presets().items().at(index);
        if (!Biz::Preset::PresetSelectionCheck::can_select_printer_preset(
                m_project_interactor.preset_interactor(),
                preset.hw_printer_config_id,
                preset.id
            ))
        {
            return;
        }

        m_project_interactor.preset_interactor()
            .select_printer_preset(preset.hw_printer_config_id, preset.id, true);
        m_project_interactor.undo_provider().take_snapshot(
            Biz::UndoSnapshotType::SelectPrinterPreset
        );
    };

    LayoutButton* button_advanced_setting = preset_row->emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::Cog,
        Biz::_u8L("Preset settings")
    );
    button_advanced_setting->set_background_color(Platform::Color::ButtonTransparent);
    button_advanced_setting->set_content_padding(5_fpx);
    button_advanced_setting->callbacks().action = [this]
    {
        if (m_advanced_dialog->opened()) {
            m_navigator.set_opened_dialog(nullptr);
        } else {
            m_navigator.set_opened_dialog(m_advanced_dialog);
        }
    };
    m_advanced_dialog->callbacks().opened = [button_advanced_setting]
    { button_advanced_setting->set_checked(true); };
    m_advanced_dialog->callbacks().closed = [button_advanced_setting]
    { button_advanced_setting->set_checked(false); };

    m_color_mix_dialog->attach_to_center();

    m_color_mix_button = preset_row->emplace_back<LayoutButton>(std::string{},
        Render::Icon::Palette,
        Biz::_u8L("Color mix settings")
    );
    m_color_mix_button->set_background_color(Platform::Color::ButtonTransparent);
    m_color_mix_button->set_content_padding(5_fpx);
    m_color_mix_button->callbacks().action = [this]
    {
        if (m_color_mix_dialog->opened()) {
            m_navigator.set_opened_dialog(nullptr);
        } else {
            m_navigator.set_opened_dialog(m_color_mix_dialog);
        }
    };
    m_color_mix_dialog->callbacks().opened = [this] { m_color_mix_button->set_checked(true); };
    m_color_mix_dialog->callbacks().closed = [this] { m_color_mix_button->set_checked(false); };

    this->update_color_mix_visibility();

    m_printer_settings_dialog
        ->attach_to_item(m_logical_printer_button, Position::Left, 40_fpx, AlignU::Start);
    m_printer_settings_dialog->callbacks().opened = [this]()
    { m_logical_printer_button->set_checked(true); };
    m_printer_settings_dialog->callbacks().closed = [this]()
    { m_logical_printer_button->set_checked(false); };

    auto toggle_printer_settings_dialog = [this]()
    {
        if (m_printer_settings_dialog->opened()) {
            m_navigator.set_opened_dialog(nullptr);
        } else {
            m_navigator.set_opened_dialog(m_printer_settings_dialog);
        }
    };
    m_logical_printer_button->callbacks().action = [toggle_printer_settings_dialog]()
    { toggle_printer_settings_dialog(); };
    m_logical_printer_button->on_cog() = [toggle_printer_settings_dialog, this]()
    {
        toggle_printer_settings_dialog();
        if (const Biz::PhysicalPrinter::PhysicalPrinterConfig* physical_printer =
                m_project_interactor.physical_printer_interactor().selected_physical_printer_data())
        {
            m_printer_settings_dialog->select_page_settings(*physical_printer);
        }
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
        [toggle_printer_settings_dialog, this](size_t index)
        {
            toggle_printer_settings_dialog();
            if (const Biz::PhysicalPrinter::PhysicalPrinterConfig* physical_printer =
                    m_project_interactor.physical_printer_interactor()
                        .selected_physical_printer_data())
            {
                m_printer_settings_dialog->select_page_settings(*physical_printer);
            }
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

    m_purging_volumes_button = emplace_back<LayoutButton>(Biz::_u8L("Purging volumes"));
    m_purging_volumes_button->callbacks().action = [this]()
    {
        Biz::ProjectSettingsInteractor& psi  = m_project_interactor.project_settings_interactor();
        const Domain::SelectionId project_id = m_project_interactor.selected_project_id();
        const Domain::SelectionId cc_id      = m_project_interactor.selected_config_container_id();

        const Biz::WipingVolumesData data = psi.get_wiping_volumes_data(project_id, cc_id);

        const std::optional<WipingDialogResult> result =
            AppServices::instance().dialog_manager().show_wiping_dialog(
                data.matrix,
                data.extruder_colours,
                data.printer_purging_volume,
                data.filament_purging_multipliers,
                data.use_custom_matrix
            );
        if (!result)
            return;

        psi.set_wiping_volumes_data(project_id, cc_id, result->matrix, result->use_custom_matrix);
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

    update_purging_volumes_button_visibility();
}

void SidebarBed::on_list_selection_changed(Domain::SelectionId new_selection)
{
    if (new_selection == Domain::INVALID_ID) {
        return;
    }

    update_printer_button();
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

void SidebarBed::update_color_mix_visibility()
{
    bool can_mix_colors = false;
    if (m_project_interactor.selected_config_container_id() != Domain::INVALID_ID) {
        const Domain::ConfigContainer& config_container =
            m_project_interactor.selected_config_container();

        can_mix_colors = config_container.print_technology() == Domain::PrinterTechnology::FFF
            && config_container.selected_preset().hw_config.material_slot_count() > 1;
    }

    m_color_mix_button->set_visible(can_mix_colors);

    if (!can_mix_colors && m_color_mix_dialog->opened()) {
        m_navigator.set_opened_dialog(nullptr);
    }
}

void SidebarBed::update_purging_volumes_button_visibility()
{
    const Domain::ConfigContainer& cc = m_project_interactor.selected_config_container();
    if (cc.print_technology() != Domain::PrinterTechnology::FFF) {
        // "wipe_tower" is an FDM-only option; the SLA print config box doesn't define it.
        m_purging_volumes_button->set_visible(false);
        return;
    }

    const Domain::Preset::SelectedPreset& selected_preset = cc.selected_preset();
    const bool has_wipe_tower =
        selected_preset.print.config_box().items.opt("wipe_tower").get<bool>();
    const bool has_single_extruder_multi_material = selected_preset.printer.config_box()
                                                        .items.opt("single_extruder_multi_material")
                                                        .get<bool>();
    const bool has_multi_materials                = selected_preset.materials.size() > 1;
    m_purging_volumes_button->set_visible(
        has_wipe_tower && has_multi_materials && has_single_extruder_multi_material
    );
}

void SidebarBed::update_printer_button()
{
    const auto& preset_interactor              = m_project_interactor.preset_interactor();
    const Biz::Preset::PresetItem& preset_item = preset_interactor.printer_presets().items().at(
        preset_interactor.printer_presets().selected_index()
    );

    const bool is_modified_preset = preset_interactor.is_printer_preset_selected_and_dirty(
        preset_item.hw_printer_config_id,
        preset_item.id
    );

    const Biz::PhysicalPrinter::PhysicalPrinterConfig* physical_printer =
        m_project_interactor.physical_printer_interactor().selected_physical_printer_data();

    m_selected_printer_preset_name = physical_printer
        ? Biz::PhysicalPrinter::ui_name(*physical_printer)
        : preset_item.ui_preset_name();
    m_logical_printer_button->set_printer_name(m_selected_printer_preset_name, is_modified_preset);
    m_logical_printer_button->set_visible_cog(
        !physical_printer
        || (!physical_printer->is_runtime && !Biz::PhysicalPrinter::is_connect_printer(*physical_printer))
    );
    const auto* connect_upload = physical_printer
        ? std::get_if<Biz::PhysicalPrinter::ConnectUpload>(&physical_printer->payload)
        : nullptr;
    m_logical_printer_button->set_connect_indicator(
        connect_upload ? std::optional{connect_upload->state} : std::nullopt
    );
    m_logical_printer_button->set_preset_name(preset_item.ui_hw_config_name());
    m_logical_printer_button->set_tooltip(
        fmt::format(
            "{}\n{}\n{}",
            m_selected_printer_preset_name,
            preset_item.ui_preset_name(),
            preset_item.ui_hw_config_name()
        )
    );

    const Domain::Preset::HwPrinterConfig& printer_config =
        preset_interactor.current_printer_config();

    if (printer_config.visual.thumbnail.has_value()) {
        const std::string image_path =
            printer_config.relative_path_to_assets() + printer_config.visual.thumbnail.value();

        m_logical_printer_button->set_image(image_path);
    }
}

void SidebarBed::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{std::get<Domain::FDMConfigLocation>(item.location())};
        if ((location == Domain::FDMConfigLocation::Printer
             && item.name() == "single_extruder_multi_material")
            || (location == Domain::FDMConfigLocation::Print && item.name() == "wipe_tower"))
        {
            update_purging_volumes_button_visibility();
        } else if (location != Domain::FDMConfigLocation::Printer) {
            return;
        }
    } else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{std::get<Domain::SLAConfigLocation>(item.location())};
        if (location != Domain::SLAConfigLocation::Printer) {
            return;
        }
    }

    refresh_printer_label_color();
}

void SidebarBed::on_preset_bundles_reloaded()
{
    refresh_printer_label_color();
}

void SidebarBed::on_hw_item_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::HwItemType type
)
{
    if (m_project_interactor.selected_project_id() == project_id
        && m_project_interactor.selected_config_container_id() == config_container_id
        && type == Biz::Preset::HwItemType::ToolItem)
    {
        update_color_mix_visibility();
    }
}

void SidebarBed::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    update_color_mix_visibility();
    if (m_project_interactor.selected_project_id() == project_id) {
        update_purging_volumes_button_visibility();
    }
}

void SidebarBed::on_selected_physical_printer_changed()
{
    update_printer_button();
}

void SidebarBed::on_selected_physical_printer_updated()
{
    update_printer_button();
}

PrinterSettingsDialog& SidebarBed::logical_printer_settings_dialog()
{
    return *m_printer_settings_dialog;
}

PrinterAddDialog& SidebarBed::printer_add_dialog()
{
    return *m_printer_add_dialog;
}

MaterialSelectionDialog& SidebarBed::material_selection_dialog()
{
    return *m_material_selection_dialog;
}

ColorMix::ColorMixDialog& SidebarBed::color_mix_dialog()
{
    return *m_color_mix_dialog;
}

PrinterAdvancedSettingsDialog& SidebarBed::printer_advanced_settings_dialog()
{
    return *m_advanced_dialog;
}

} // namespace Slic3r::App
