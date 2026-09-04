#include "Slic3r/Biz/ObservableProjectList.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::Biz {

ObservableProjectList::ObservableProjectList(Biz::ProjectInteractor& project_interactor) :
    m_project_changed_listener_scope(project_interactor, *this),
    m_backup_store_listener_scope(project_interactor.backup_store(), *this),
    m_project_interactor(project_interactor)
{
    const Domain::Workbench::ProjectMap& project_map = m_project_interactor.workbench().projects();
    m_projects.reserve(project_map.size());
    for (const auto& [id, project] : project_map) {
        m_projects.emplace_back(std::make_unique<Domain::SelectionId>(id));
    }
}

const Domain::SelectionId& ObservableProjectList::at(size_t index) const
{
    return *m_projects.at(index).get();
}

size_t ObservableProjectList::size() const
{
    return m_projects.size();
}

void ObservableProjectList::on_project_added_uninitialized(Domain::SelectionId project_id)
{
    m_projects.emplace_back(std::make_unique<Domain::SelectionId>(project_id));

    invoke_listeners<Biz::IListObserver<Domain::SelectionId>>(
        [&](auto* l)
        {
            const size_t index = m_projects.size() - 1;
            l->on_inserted(at(index), index);
        }
    );
}

void ObservableProjectList::on_project_removed(Domain::SelectionId project_id)
{
    Projects::const_iterator it = std::find_if(
        m_projects.cbegin(),
        m_projects.cend(),
        [&project_id](const std::unique_ptr<Domain::SelectionId>& ptr)
        { return *ptr == project_id; }
    );
    if (it == m_projects.cend()) {
        return;
    }

    const size_t index = std::distance(m_projects.cbegin(), it);
    invoke_listeners<Biz::IListObserver<Domain::SelectionId>>([&](auto* l)
                                                              { l->on_will_be_removed({index}); });

    m_projects.erase(it);

    invoke_listeners<Biz::IListObserver<Domain::SelectionId>>([&](auto* l)
                                                              { l->on_removed({index}); });
}

void ObservableProjectList::on_project_changed(Domain::SelectionId project_id)
{
    if (!m_project_interactor.workbench().find_project_by_id(project_id)) {
        // project does not exists and was probably removed
        return;
    }

    Projects::const_iterator it = std::find_if(
        m_projects.cbegin(),
        m_projects.cend(),
        [&project_id](const std::unique_ptr<Domain::SelectionId>& ptr)
        { return *ptr == project_id; }
    );
    if (it == m_projects.cend()) {
        return;
    }

    invoke_listeners<Biz::IListObserver<Domain::SelectionId>>(
        [&](auto* l) { l->on_updated(std::distance(m_projects.cbegin(), it)); }
    );
}

void ObservableProjectList::on_project_invalidation_changed(Domain::SelectionId project_id)
{
    on_project_changed(project_id);
}

} // namespace Slic3r::Biz
