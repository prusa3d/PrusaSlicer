#include "Slic3r/App/ObjectListWindow.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/FDMResultCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"

#include "Slic3r/App/ObjectListItem.hpp"

#include "Slic3r/App/Plater/BedThumbnailTexture.hpp"

#include "Slic3r/LegacyFormat.hpp"
#include "Slic3r/Time.hpp"

#include <boost/nowide/convert.hpp>
#include <boost/nowide/iostream.hpp>

using namespace Slic3r::Biz;

namespace Slic3r::App {

using namespace Yoga;

ObjectListWindow::ObjectListWindow(Biz::ProjectInteractor& project_interactor, bool for_plater) :
    CollapsibleWindow(_u8L("Object list"), "ObjectListWindow"),
    m_project_interactor(project_interactor)
{
    set_transparent(true);
    content()->set_padding(0.f);
    const ObjectListItem::Mode mode =
        for_plater ? ObjectListItem::Mode::Plater : ObjectListItem::Mode::Preview;
    if (mode == ObjectListItem::Mode::Plater) {
        m_add_container_button = content()->emplace_back<Yoga::LayoutButton>(
            _u8L("Add Printer Group"),
            Render::Icon::ConfigContainer
        );
        m_add_container_button->set_flex_shrink(0.f);
        m_add_container_button->set_padding({20.f, 5.f});
        m_add_container_button->callbacks().action = [this]()
        {
            m_project_interactor.add_config_container();
            if (on_config_container_added != nullptr) {
                on_config_container_added();
                m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::AddConfigContainer);
            }
        };
    }

    m_object_list = content()->emplace_back<ObjectListItem>(project_interactor, mode);

    if (mode == ObjectListItem::Mode::Preview) {
        m_sliced_info = content()->emplace_back<Rectangle>();
        m_sliced_info->set_orientation(Orientation::Vertical);
        m_sliced_info->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
        m_sliced_info->set_flags(ImDrawFlags_RoundCornersBottom);
        m_sliced_info->set_padding(15_fpx);
        m_sliced_info->set_gap(10_fpx);
        m_sliced_info->set_visible(false);
        m_sliced_info->set_flex_shrink(0.f);

        Item* si_header = m_sliced_info->emplace_back<Item>();
        Text* si_label  = si_header->emplace_back<Text>(_u8L("Sliced Info"));
        si_label->set_font_type(Render::ImguiFontType::Bold);
        si_label->set_flex_grow(1.f);

        /*// temporary hide unused button
        LayoutButton* calc_btn = si_header->emplace_back<LayoutButton>("", Render::Icon::Calculator);
        */

        auto add_row = [this](const std::string& label, Text** value, bool visible = false) -> Item*
        {
            Item* row = m_sliced_info->emplace_back<Item>();
            row->set_gap(10_fpx);

            Text* label_text = row->emplace_back<Text>(label);
            label_text->set_font_type(Render::ImguiFontType::Bold);
            label_text->set_width(85_fpx);

            *value = row->emplace_back<Text>("");
            (*value)->set_flex_grow(1);
            (*value)->set_wrap_mode(Text::WrapMode::WrapElide);

            row->set_visible(visible);
            return row;
        };

        add_row(_u8L("Used material"), &m_used_material_total, true);

        const std::string space{"   "};
        m_used_material_for_objects_row =
            add_row(space + _u8L("objects"), &m_used_material_for_objects);

        m_used_material_for_wipe_tower_row =
            add_row(space + _u8L("wipe tower"), &m_used_material_for_wipe_tower);

        m_used_material_for_flush_row = add_row(space + _u8L("flush"), &m_used_material_for_flush);

        m_used_material_for_supports_row =
            add_row(space + _u8L("supports and pad"), &m_used_material_for_supports);

        m_material_cost_row = add_row(_u8L("Cost"), &m_material_cost);
        m_material_cost_row->set_visible(false);

        m_first_layer_time_row = add_row(_u8L("First layer"), &m_first_layer_time);
        m_first_layer_time_row->set_visible(false);

        add_row(_u8L("Printing time"), &m_estimated_time, true);
    }
}

static std::string
format_used_material(const float weight_g, const float length_mm, const float volume_cm3)
{
    return format("%1$.2f g  %2$.2f m  %3$.0f mm3", weight_g, length_mm / 1000.f, volume_cm3);
}

void ObjectListWindow::update_sliced_info()
{
    const Domain::SlicingId id = m_project_interactor.selected_bed_slicing_id();
    const std::optional<Biz::Slicing::Status> status{
        m_project_interactor.status_cache().get_status(id) };

    const bool is_finished = status && status->code == Biz::Slicing::StatusCode::Finished;
    m_sliced_info->set_visible(is_finished);

    if (!is_finished) {
        return;
    }

    const std::optional<Biz::FDMResultRef> fdm_result{ m_project_interactor.fdm_result_cache().get_result(id) };
    if (fdm_result) {
        const auto* print_statistics_ptr =
            std::get_if<Domain::FullPrintStatistics>(&fdm_result->get().print_statistics);
        if (!print_statistics_ptr) {
            return;
        }
        const Domain::FullPrintStatistics& print_statistics = *print_statistics_ptr;

        const float volume{print_statistics.total_used_filament_cm3};
        const float weight{print_statistics.total_used_filament_g};
        const float length{print_statistics.total_used_filament_mm};

        m_used_material_total->set_text(format_used_material(weight, length, volume));

        const bool has_wipe_tower{print_statistics.total_used_filament_for_wipe_tower_mm > 0.f};
        const bool has_flush{print_statistics.total_used_filament_for_flush_mm > 0.f};

        m_used_material_for_objects->set_text(format_used_material(
            weight
                - print_statistics.total_used_filament_for_wipe_tower_g
                - print_statistics.total_used_filament_for_flush_g,
            length
                - print_statistics.total_used_filament_for_wipe_tower_mm
                - print_statistics.total_used_filament_for_flush_mm,
            volume
                - print_statistics.total_used_filament_for_wipe_tower_cm3
                - print_statistics.total_used_filament_for_flush_cm3
        ));
        m_used_material_for_objects_row->set_visible(has_wipe_tower || has_flush);

        m_used_material_for_wipe_tower->set_text(format_used_material(
            print_statistics.total_used_filament_for_wipe_tower_g,
            print_statistics.total_used_filament_for_wipe_tower_mm,
            print_statistics.total_used_filament_for_wipe_tower_cm3
        ));
        m_used_material_for_wipe_tower_row->set_visible(has_wipe_tower);

        m_used_material_for_flush->set_text(format_used_material(
            print_statistics.total_used_filament_for_flush_g,
            print_statistics.total_used_filament_for_flush_mm,
            print_statistics.total_used_filament_for_flush_cm3
        ));
        m_used_material_for_flush_row->set_visible(has_flush);
        m_used_material_for_supports_row->set_visible(false);

        const float cost{print_statistics.total_filament_cost};
        m_material_cost->set_text(format("%1%", cost));

        const std::string first_layer_time = "? seconds";
        m_first_layer_time->set_text(first_layer_time);

        const std::string estimated_time =
            Utils::get_time_dhms(print_statistics.normal_mode_time.time);
        m_estimated_time->set_text(estimated_time);

        return;
    }

    const std::optional<Biz::SLAResultRef> sla_result{ m_project_interactor.sla_result_cache().get_result(id) };
    if (!sla_result || !sla_result->get().export_data->print_statistics)
        return;

    const Domain::SLA::PrintStatistics& print_statistics =
        *sla_result->get().export_data->print_statistics;

    // Resin is measured in ml, the statistics are in mm3.
    const auto format_ml = [](const double volume_mm3) {
        return format("%1$.2f ml", volume_mm3 / 1000.);
    };
    const double objects_volume{print_statistics.objects_used_material};
    const double supports_volume{print_statistics.support_used_material};
    const bool has_supports{supports_volume > 0.};

    m_used_material_total->set_text(format_ml(objects_volume + supports_volume));
    m_used_material_for_objects->set_text(format_ml(objects_volume));
    m_used_material_for_objects_row->set_visible(has_supports);
    m_used_material_for_supports->set_text(format_ml(supports_volume));
    m_used_material_for_supports_row->set_visible(has_supports);
    m_used_material_for_wipe_tower_row->set_visible(false);
    m_used_material_for_flush_row->set_visible(false);

    m_material_cost->set_text(format("%1%", print_statistics.total_cost));

    m_first_layer_time->set_text(
        format("%1% seconds", print_statistics.layers_times_running_total[0])
    );

    const std::string estimated_time = Utils::get_time_dhms(print_statistics.estimated_print_time);
    m_estimated_time->set_text(estimated_time);
}

void ObjectListWindow::set_bed_instance_icons(const Plater::BedThumbnailTextures& icons)
{
    m_object_list->set_bed_instance_icons(icons);
}

void ObjectListWindow::set_gizmo_controller(Scene::IGizmoController* controller)
{
    // Not yet consumed by ObjectListItem; stored for when gizmo-triggering rows are ported.
    m_gizmo_controller = controller;
}

void ObjectListWindow::add_context_menu_listener(IShowContextMenuListener* listener)
{
    m_object_list->add_context_menu_listener(listener);
}

void ObjectListWindow::start_renaming_selected()
{
    set_collapsed(false);
    m_object_list->start_renaming_selected();
}

} // namespace Slic3r::App
