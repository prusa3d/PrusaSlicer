#pragma once

#include <map>

#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::Biz {

/**
 * @brief Container for project-specific data with automatic data creation / destruction as projects
 * gets opened and removed.
 * @tparam T Structure holding project specific data.
 */
template <typename T>
class ProjectScoped : private IProjectsChangedListener {
public:

    /**
     * @brief Creates new container for project-specific data.
     *
     * The instance gets registered as IProjectsChangedListener to the passed project_interactor.
     * @param project_interactor a project interactor to bound to.
     */
    explicit ProjectScoped(ProjectInteractor& project_interactor)
        : m_project_interactor(project_interactor)
    {
        m_project_interactor.add_listener<IProjectsChangedListener>(this);
        const auto& projs = m_project_interactor.workbench().projects();
        for (const auto& [project_id, _] : projs) {
            m_projects.emplace(project_id, factory());
        }
    }

    /**
     * @brief Unregister as IProjectsChangedListener from bound project interactor and destroy data.
     */
    ~ProjectScoped() override {
        m_project_interactor.remove_listener<IProjectsChangedListener>(this);
    }

    T& project(size_t project_id)
    {
        auto it = m_projects.find(project_id);
        ASSERT(it != m_projects.end());
        return it->second;
    }

    const T& project(size_t project_id) const
    {
        auto it = m_projects.find(project_id);
        ASSERT(it != m_projects.end());
        return it->second;
    }

    T& selected()
    {
        return project(m_project_interactor.selected_project_id());
    }

    const T& selected() const
    {
        return project(m_project_interactor.selected_project_id());
    }

    std::vector<size_t> get_project_ids() const
    {
        std::vector<size_t> ids;
        ids.reserve(m_projects.size());
        for (const auto& [id, _] : m_projects)
            ids.push_back(id);
        return ids;
    }

protected:
    T factory() { return {}; }
private:
    void on_project_added_uninitialized(Domain::SelectionId project_id) override
    {
        m_projects.emplace(project_id, factory());
    }

    void on_project_removed(Domain::SelectionId project_id) override
    {
        m_projects.erase(project_id);
    }
private:
    using Projects = std::map<size_t, T>;
    ProjectInteractor& m_project_interactor;
    Projects m_projects;
};

}
