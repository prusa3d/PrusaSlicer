#include "Slic3r/App/ObjectListWindow.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/FDMResultCache.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/ObjectList.hpp"

#include "Slic3r/App/Plater/BedThumbnailTexture.hpp"

#include "Slic3r/LegacyFormat.hpp"
#include "Slic3r/Time.hpp"

#include <boost/nowide/convert.hpp>
#include <boost/nowide/iostream.hpp>

using namespace Slic3r::Biz;

namespace Slic3r::App {

using namespace Yoga;

ObjectListWindow::ObjectListWindow(Biz::ProjectInteractor* project_interactor, bool for_plater) :
    CollapsibleWindow(_u8L("Object list"), "ObjectListWindow"),
    m_project_interactor(project_interactor)
{
    content()->set_padding(0.f);
    const ObjectList::Mode mode = for_plater ? ObjectList::Mode::Plater : ObjectList::Mode::Preview;
    if (mode == ObjectList::Mode::Plater) {
        m_add_container_button = content()->emplace_back<Yoga::LayoutButton>(
            _u8L("Add Printer Group"),
            Render::Icon::ConfigContainer
        );
        m_add_container_button->set_flex_shrink(0.f);
        m_add_container_button->set_padding({20.f, 5.f});
        m_add_container_button->callbacks().action = [this]()
        {
            m_project_interactor->add_config_container();
            if (on_config_container_added != nullptr) {
                on_config_container_added();
                m_project_interactor->undo_provider().take_snapshot(Biz::UndoSnapshotType::AddConfigContainer);
            }
        };
    }

    m_scroll_area = content()->emplace_back<ScrollArea>();
    m_scroll_area->set_min_height(100);

    m_object_list = m_scroll_area->emplace_back<ObjectList>(project_interactor, mode);
    m_object_list->set_flex_shrink(0.f);
    m_object_list->set_horizontal_padding(15.f);

    init_cc_context_menu();

    if (mode == ObjectList::Mode::Plater) {
        return; // temporary hide unused button
        LayoutButton* show_details_button = emplace_into_header<LayoutButton>("", Render::Icon::Details, _u8L("Show item details"));
        show_details_button->set_checkable(true);
        show_details_button->callbacks().checked_changed = [this](bool checked) {
            m_object_list->selected_project_context().show_details = checked;
        };
    } else {
        m_sliced_info = content()->emplace_back<Rectangle>();
        m_sliced_info->set_orientation(Orientation::Vertical);
        m_sliced_info->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
        m_sliced_info->set_flags(ImDrawFlags_RoundCornersBottom);
        m_sliced_info->set_padding(15.f);
        m_sliced_info->set_gap(10.f);
        m_sliced_info->set_visible(false);
        m_sliced_info->set_flex_shrink(0.f);

        Item* si_header = m_sliced_info->emplace_back<Item>();
        Text* si_label = si_header->emplace_back<Text>(_u8L("Sliced Info"));
        si_label->set_font_type(Render::ImguiFontType::Bold);
        si_label->set_flex_grow(1.f);
        /*// temporary hide unused button
        LayoutButton* calc_btn = si_header->emplace_back<LayoutButton>("", Render::Icon::Calculator);
        */

        struct SlicedInfoRow
        {
            Item* row;
            Text* label;
            Text* value;
        };

        auto add_row = [this](const std::string& label) -> SlicedInfoRow
        {
            Item* row = m_sliced_info->emplace_back<Item>();
            row->set_gap(10.f);

            Text* label_text = row->emplace_back<Text>(label);
            label_text->set_font_type(Render::ImguiFontType::Bold);
            label_text->set_width(85.f);

            Text* value_text = row->emplace_back<Text>("");
            value_text->set_flex_grow(1);
            value_text->set_wrap_mode(Text::WrapMode::WrapElide);
            return {row, label_text, value_text};
        };

        const SlicedInfoRow used_material_row = add_row(_u8L("Used material"));
        m_used_material_label                 = used_material_row.label;
        m_used_material                       = used_material_row.value;

        const SlicedInfoRow material_cost_row = add_row(_u8L("Cost"));
        m_material_cost_row                   = material_cost_row.row;
        m_material_cost                       = material_cost_row.value;
        m_material_cost_row->set_visible(false);

        const SlicedInfoRow first_layer_time_row = add_row(_u8L("First layer"));
        m_first_layer_time_row                   = first_layer_time_row.row;
        m_first_layer_time                       = first_layer_time_row.value;
        m_first_layer_time_row->set_visible(false);

        m_estimated_time = add_row(_u8L("Printing time")).value;
    }
}

static std::string
format_used_material(const float weight_g, const float length_mm, const float volume_cm3)
{
    return format("%1$.2f g  %2$.2f m  %3$.0f mm3", weight_g, length_mm / 1000.f, volume_cm3);
}

void ObjectListWindow::update_sliced_info()
{
    const Domain::SlicingId id = m_project_interactor->selected_bed_slicing_id();
    const std::optional<Biz::Slicing::Status> status{
        m_project_interactor->status_cache().get_status(id) };

    const bool is_finished = status && status->code == Biz::Slicing::StatusCode::Finished;
    m_sliced_info->set_visible(is_finished);

    if (!is_finished) {
        return;

    }

    const std::optional<Biz::FDMResultRef> fdm_result{ m_project_interactor->fdm_result_cache().get_result(id) };
    if (fdm_result) {
        const auto* print_statistics_ptr = std::get_if<Domain::FullPrintStatistics>(&fdm_result->get().print_statistics);
        if (!print_statistics_ptr) {
            return;
        }
        const Domain::FullPrintStatistics& print_statistics = *print_statistics_ptr;

        const float volume{print_statistics.total_used_filament_cm3};
        const float weight{print_statistics.total_used_filament_g};
        const float length{print_statistics.total_used_filament_mm};

        const bool has_wipe_tower{print_statistics.total_used_filament_for_wipe_tower_mm > 0.f};
        const bool has_flush{print_statistics.total_used_filament_for_flush_mm > 0.f};

        std::string used_material_label{_u8L("Used material")};
        std::string used_material{format_used_material(weight, length, volume)};

        const auto append_row =
            [&](const std::string& label, float weight_g, float length_mm, float volume_cm3)
        {
            used_material_label += "\n  " + label;
            used_material += '\n' + format_used_material(weight_g, length_mm, volume_cm3);
        };

        if (has_wipe_tower || has_flush) {
            append_row(
                _u8L("objects"),
                weight
                    - print_statistics.total_used_filament_for_wipe_tower_g
                    - print_statistics.total_used_filament_for_flush_g,
                length
                    - print_statistics.total_used_filament_for_wipe_tower_mm
                    - print_statistics.total_used_filament_for_flush_mm,
                volume
                    - print_statistics.total_used_filament_for_wipe_tower_cm3
                    - print_statistics.total_used_filament_for_flush_cm3
            );

            if (has_wipe_tower) {
                append_row(
                    _u8L("wipe tower"),
                    print_statistics.total_used_filament_for_wipe_tower_g,
                    print_statistics.total_used_filament_for_wipe_tower_mm,
                    print_statistics.total_used_filament_for_wipe_tower_cm3
                );
            }

            if (has_flush) {
                append_row(
                    _u8L("flush"),
                    print_statistics.total_used_filament_for_flush_g,
                    print_statistics.total_used_filament_for_flush_mm,
                    print_statistics.total_used_filament_for_flush_cm3
                );
            }
        }

        m_used_material_label->set_text(used_material_label);
        m_used_material->set_text(used_material);

        const float cost{ print_statistics.total_filament_cost };
        m_material_cost->set_text(format("%1%", cost));

        const std::string first_layer_time = "? seconds";
        m_first_layer_time->set_text(first_layer_time);

        const std::string estimated_time = Utils::get_time_dhms(print_statistics.normal_mode_time.time);
        m_estimated_time->set_text(estimated_time);

        return;
    }

    const std::optional<Biz::SLAResultRef> sla_result{ m_project_interactor->sla_result_cache().get_result(id) };
    if (!sla_result || !sla_result->get().export_data->print_statistics)
        return;

    const Domain::SLA::PrintStatistics& print_statistics = *sla_result->get().export_data->print_statistics;

    float used_material_total = print_statistics.objects_used_material + print_statistics.support_used_material;

    const std::string used_material = format("%1$.2f g  %2$.0f mm3", print_statistics.total_weight, used_material_total);
    m_used_material_label->set_text(_u8L("Used material"));
    m_used_material->set_text(used_material);

    m_material_cost->set_text(format("%1%", print_statistics.total_cost));

    m_first_layer_time->set_text(format("%1% seconds", print_statistics.layers_times_running_total[0]));

    const std::string estimated_time = Utils::get_time_dhms(print_statistics.estimated_print_time);
    m_estimated_time->set_text(estimated_time);
}

void ObjectListWindow::set_bed_instance_icons(const Plater::BedThumbnailTextures& icons)
{
    m_object_list->set_bed_instance_icons(icons);
}

void ObjectListWindow::set_gizmo_controller(Scene::IGizmoController* controller)
{
    m_object_list->set_gizmo_controller(controller);
}

void ObjectListWindow::init_cc_context_menu()
{
    // Create context menu

    m_cc_context_menu =
        content()->emplace_back<Yoga::Menu>("cc_context_menu", Yoga::Position::Top);

    m_delete_cc_menu_item =
        m_cc_context_menu->append_item(_u8L("Delete"), Render::Icon::DeleteBtnIcon);
    m_delete_cc_menu_item->callbacks().action = [this]()
    {
        m_project_interactor->remove_config_container(m_selected_config_container_id);
        m_project_interactor->undo_provider().take_snapshot(
            Biz::UndoSnapshotType::DeleteConfigContainer
        );
    };

    m_cc_context_menu->append_item(_u8L("Duplicate"), Render::Icon::CopyForGizmo)
        ->callbacks()
        .action = [this]()
    {
        m_project_interactor->duplicate_config_container(m_selected_config_container_id);
        m_project_interactor->undo_provider().take_snapshot(
            Biz::UndoSnapshotType::DuplicateConfigContainer
        );
    };

    // Process callback for show context menu

    m_object_list->callbacks().show_context_menu =
        [&](Domain::Vec2f open_pos, Domain::SelectionId config_container_id)
    {
        ASSERT(config_container_id != Domain::INVALID_ID);
        // Don't allow to delete last config container
        const size_t containers_cnt =
            m_project_interactor->scene_interactor().selected_project_config_containers().size();
        m_delete_cc_menu_item->set_enabled(containers_cnt > 1);

        m_selected_config_container_id = config_container_id;
        m_cc_context_menu->open(open_pos-content()->get_global_pos());
    };
}

} // namespace Slic3r::App
