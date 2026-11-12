#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/Preset/Bundle.hpp"
#include "Slic3r/IdGenerator.hpp"

namespace Slic3r::Domain {

class Workbench
{
public:
    using ProjectMap = std::unordered_map<SelectionId, Project>;

    Workbench();
    Workbench(Workbench&&) = default;

    Workbench(const Workbench&) = delete;
    Workbench& operator=(const Workbench&) = delete;

    [[nodiscard]] ProjectMap& projects() { return m_projects; }
    [[nodiscard]] const ProjectMap& projects() const { return m_projects; }

    [[nodiscard]] Project& project(const size_t project_id) { auto it = m_projects.find(project_id); ASSERT(it != m_projects.end()); return it->second; }
    [[nodiscard]] const Project& project(const size_t project_id) const { auto it = m_projects.find(project_id); ASSERT(it != m_projects.end()); return it->second; }

    [[nodiscard]] Project* find_project_by_id(const size_t project_id) { auto it = m_projects.find(project_id); if (it == m_projects.end()) return nullptr; return &it->second; }
    [[nodiscard]] const Project* find_project_by_id(const size_t project_id) const { auto it = m_projects.find(project_id); if (it == m_projects.end()) return nullptr; return &it->second; }

    [[nodiscard]] bool has_preset_bundle() const { return m_preset_bundle != nullptr; }
    [[nodiscard]] const Preset::Bundle& preset_bundle() const { return *m_preset_bundle; }
    [[nodiscard]] Preset::Bundle& preset_bundle() { return  *m_preset_bundle; }

    void set_preset_bundle(Preset::Bundle&& preset_bundle)
    { m_preset_bundle = std::make_unique<Preset::Bundle>(std::forward<Preset::Bundle>(preset_bundle)); }

    SelectionId next_project_id() { return m_project_id_generator.next_id(); }
private:
    ProjectMap m_projects;
    IdGenerator<SelectionId> m_project_id_generator;
    std::shared_ptr<Preset::Bundle> m_preset_bundle;
};

} // namespace Slic3r::Domain
