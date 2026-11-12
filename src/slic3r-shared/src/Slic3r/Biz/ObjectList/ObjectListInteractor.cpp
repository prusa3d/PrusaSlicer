#include "Slic3r/Biz/ObjectList/ObjectListInteractor.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::Biz::ObjectList {

void ObjectListProjectContext::
    ensure(Biz::ProjectInteractor& project_interactor, Domain::SelectionId project_id)
{
    if (!printer_groups) {
        printer_groups = std::make_shared<ConfigContainerObservableList>(
            project_interactor,
            project_interactor.project(project_id).config_containers(),
            project_id
        );
    }
    if (!out_of_bed_objects) {
        out_of_bed_objects =
            std::make_shared<ObjectObservableList>(project_interactor, project_id);
    }
}

ObjectListInteractor::ObjectListInteractor(Scene::SceneInteractor& scene_interactor) :
    m_scene_interactor(scene_interactor),
    m_config_container_list_changed_listener_scope(scene_interactor, *this)
{}

ObjectListInteractor::~ObjectListInteractor() = default;

void ObjectListInteractor::bind_project_interactor(Biz::ProjectInteractor& project_interactor)
{
    ASSERT(!m_project_interactor);
    m_project_interactor = &project_interactor;
    m_project_contexts.emplace(project_interactor);
}

ObjectListProjectContext& ObjectListInteractor::context(Domain::SelectionId project_id)
{
    ASSERT(m_project_interactor && m_project_contexts);
    ObjectListProjectContext& ctx = m_project_contexts->project(project_id);
    ctx.ensure(*m_project_interactor, project_id);
    return ctx;
}

void ObjectListInteractor::refresh(Domain::SelectionId project_id)
{
    if (!m_project_interactor || !m_project_interactor->project_exists(project_id)) {
        return;
    }

    context(project_id).printer_groups->update_data();
}

std::weak_ptr<ConfigContainerObservableList>
ObjectListInteractor::printer_groups(Domain::SelectionId project_id)
{
    return context(project_id).printer_groups.get();
}

std::weak_ptr<ObjectObservableList>
ObjectListInteractor::out_of_bed_objects(Domain::SelectionId project_id)
{
    return context(project_id).out_of_bed_objects.get();
}

void ObjectListInteractor::on_config_container_list_changed(Domain::SelectionId project_id)
{
    refresh(project_id);
}

void ObjectListInteractor::on_config_container_bed_list_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id
)
{
    if (!m_project_interactor || !m_project_interactor->project_exists(project_id)) {
        return;
    }
    ObjectListProjectContext& ctx = context(project_id);
    for (size_t i = 0; i < ctx.printer_groups->size(); ++i) {
        const PrinterGroup& group = ctx.printer_groups->at(i);
        if (group.config_container_id == config_container_id) {
            group.beds_list->update_data();
            break;
        }
    }
}

void ObjectListInteractor::on_project_will_be_removed(Domain::SelectionId project_id)
{
    // Fires while the project still exists in the workbench (unlike on_project_removed, which
    // fires after it's already erased) - use that window to unregister listeners without
    // lazily building a tree for a project whose object list was never displayed (context()
    // would call ensure(), which needs project_interactor.project(project_id) to still resolve).
    if (!m_project_interactor || !m_project_contexts) {
        return;
    }
    ObjectListProjectContext& ctx = m_project_contexts->project(project_id);
    if (ctx.printer_groups) {
        ctx.printer_groups->remove_from_listeners();
    }
}

void ObjectListInteractor::on_project_loaded(Domain::SelectionId project_id)
{
    refresh(project_id);
}

} // namespace Slic3r::Biz::ObjectList
