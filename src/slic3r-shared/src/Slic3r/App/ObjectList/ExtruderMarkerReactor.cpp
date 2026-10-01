#include "Slic3r/App/ObjectList/ExtruderMarkerReactor.hpp"

#include "Slic3r/App/ExtruderIcon.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

#include "Slic3r/Domain/ConfigContainer.hpp"

namespace Slic3r::App::ObjectList {

void ExtruderMarkerReactor::bind(
    Biz::ProjectInteractor& project_interactor,
    std::optional<size_t> config_container_id,
    std::function<std::set<unsigned>()> get_candidates,
    std::function<void(const Result&)> on_refreshed
)
{
    m_project_interactor  = &project_interactor;
    m_config_container_id = config_container_id;
    m_get_candidates      = std::move(get_candidates);
    m_on_refreshed        = std::move(on_refreshed);

    if (config_container_id) {
        m_colors_listener_scope.emplace(project_interactor.project_settings_interactor(), *this);
        m_preset_listener_scope.emplace(project_interactor.preset_interactor(), *this);
        m_virtual_extruders_listener_scope
            .emplace(project_interactor.virtual_extruder_interactor(), *this);
        m_scene_changed_listener_scope.emplace(project_interactor.scene_interactor(), *this);
    }

    refresh();
}

void ExtruderMarkerReactor::on_colors_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const std::vector<Domain::ColorRGB>& colors
)
{
    if (m_config_container_id && config_container_id == *m_config_container_id) {
        refresh();
    }
}

void ExtruderMarkerReactor::on_virtual_extruders_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    if (m_config_container_id && config_container_id == *m_config_container_id) {
        refresh();
    }
}

void ExtruderMarkerReactor::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (m_config_container_id
            && config_container_id == *m_config_container_id
            && item.name() == "extruder"
        || item.name().ends_with("_extruder"))
    {
        refresh();
    }
}

void ExtruderMarkerReactor::on_volume_facets_annotations_changed(
    Domain::SelectionId project_id,
    Domain::FacetsAnnotationKind kind,
    const Domain::ElementRefs& volumes
)
{
    if (m_config_container_id && kind == Domain::FacetsAnnotationKind::MultiMaterial) {
        refresh();
    }
}

void ExtruderMarkerReactor::set_config_container(std::optional<size_t> config_container_id)
{
    m_config_container_id = config_container_id;
    refresh();
}

void ExtruderMarkerReactor::refresh()
{
    if (!m_on_refreshed) {
        return;
    }
    if (!m_project_interactor || !m_config_container_id) {
        m_on_refreshed(Result{});
        return;
    }

    const Domain::ConfigContainer* config_container =
        m_project_interactor->selected_project().find_config_container(*m_config_container_id);
    if (!config_container
        || config_container->print_technology() == Domain::PrinterTechnology::SLA
        || config_container->selected_preset().hw_config.material_slot_count() <= 1)
    {
        m_on_refreshed(Result{});
        return;
    }

    const std::set<unsigned> candidates =
        m_get_candidates ? m_get_candidates() : std::set<unsigned>{};
    if (candidates.empty()) {
        m_on_refreshed(Result{});
        return;
    }

    const std::vector<Domain::ColorRGB> extruder_colors =
        m_project_interactor->project_settings_interactor().get_colors(*m_config_container_id);

    Result result;
    result.visible = true;
    result.colors.reserve(candidates.size());
    for (unsigned candidate : candidates) {
        if (candidate >= extruder_colors.size()) {
            result.has_invalid_extruder = true;
            continue;
        }
        const Domain::ColorRGB& color = extruder_colors[candidate];
        result.colors.emplace_back(color.r(), color.g(), color.b(), 1.f);
    }
    if (candidates.size() == 1 && !result.has_invalid_extruder) {
        result.single_extruder_index = *candidates.begin() + 1;
    }

    m_on_refreshed(result);
}

void refresh_extruder_icon(ExtruderIcon* extruder_icon, const ExtruderMarkerReactor::Result& result)
{
    extruder_icon->set_visible(result.visible);
    if (!result.visible) {
        return;
    }
    if (result.has_invalid_extruder) {
        extruder_icon->set_invalid();
    } else {
        extruder_icon->set_extruders_colors(result.colors);
        extruder_icon->set_extruder_index_visible(result.single_extruder_index.has_value());
        if (result.single_extruder_index) {
            extruder_icon->set_extruder_index(*result.single_extruder_index);
        }
    }
}

} // namespace Slic3r::App::ObjectList
