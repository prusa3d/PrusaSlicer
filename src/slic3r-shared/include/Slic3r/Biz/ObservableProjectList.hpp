#pragma once

#include <Slic3r/Domain/SelectionId.hpp>
#include <Slic3r/Biz/Platform/ListenerScope.hpp>

#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/IBackupStoreListener.hpp"

#include <vector>

namespace Slic3r::Biz {

class ProjectInteractor;
class BackupStore;

class ObservableProjectList :
    public IObservableList<Domain::SelectionId>,
    public IProjectsChangedListener,
    public IBackupStoreListener
{
public:
    explicit ObservableProjectList(ProjectInteractor& project_interactor);

    const Domain::SelectionId& at(size_t index) const override;

    size_t size() const override;

    void on_project_added_uninitialized(Domain::SelectionId project_id) override;
    void on_project_removed(Domain::SelectionId project_id) override;
    void on_project_changed(Domain::SelectionId project_id) override;
    void on_project_invalidation_changed(Domain::SelectionId project_id) override;

private:
    ListenerScope<IProjectsChangedListener, ProjectInteractor, ObservableProjectList>
        m_project_changed_listener_scope;

    ListenerScope<IBackupStoreListener, BackupStore, ObservableProjectList>
        m_backup_store_listener_scope;

    ProjectInteractor& m_project_interactor;
    using Projects = std::vector<std::unique_ptr<Domain::SelectionId>>;
    Projects m_projects;
};

} // namespace Slic3r::Biz
