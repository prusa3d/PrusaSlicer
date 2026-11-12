#pragma once

#include "Slic3r/Biz/ObjectList/ConfigContainerObservableList.hpp"

#include "Slic3r/Biz/IConfigContainerListChangedListener.hpp"
#include "Slic3r/Biz/ProjectScoped.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::Biz::Slicing {
struct WipeTowerGeometry;
} // namespace Slic3r::Biz::Slicing

namespace Slic3r::Biz::ObjectList {

/**
 * @brief Per-project storage: each open project gets its own PrinterGroup tree, kept alive and
 * lazily built on first touch (see ensure()) since project id alone isn't enough to construct
 * the underlying ConfigContainerObservableList - that also needs a live ProjectInteractor&,
 * which is only available once bind_project_interactor() has run.
 */
struct ObjectListProjectContext
{
    Biz::UnsharedPointer<ConfigContainerObservableList> printer_groups;
    Biz::UnsharedPointer<ObjectObservableList> out_of_bed_objects;

    void ensure(Biz::ProjectInteractor& project_interactor, Domain::SelectionId project_id);
};

/**
 * @brief Owns the whole object-list tree (PrinterGroup -> Bed -> Object -> Volume/Instance),
 * one per open project, and is the sole listener driving its refresh: App only reads
 * printer_groups(project_id) and shows it, it never calls update_data()/on_reset() itself (the
 * one exception being which project's tree is currently displayed - see ObjectListItem).
 *
 * Owned by Scene::SceneInteractor (see SceneInteractor::object_list_interactor()). Scene-level
 * listener registration happens immediately at construction, since SceneInteractor is `this`'s
 * owner and thus already fully constructable as a publisher. Registration on ProjectInteractor
 * happens later via bind_project_interactor(), since SceneInteractor is constructed before those
 * siblings exist inside ProjectInteractor
 */
class ObjectListInteractor :
    public IConfigContainerListChangedListener,
    public IProjectsChangedListener
{
public:
    explicit ObjectListInteractor(Scene::SceneInteractor& scene_interactor);
    ~ObjectListInteractor() override;

    /**
     * @brief Completes wiring to the owning ProjectInteractor. Must be called exactly once,
     * right after all of ProjectInteractor's siblings (Preset::PresetInteractor, StatusCache)
     * have been constructed.
     */
    void bind_project_interactor(Biz::ProjectInteractor& project_interactor);

    std::weak_ptr<ConfigContainerObservableList> printer_groups(Domain::SelectionId project_id);
    std::weak_ptr<ObjectObservableList> out_of_bed_objects(Domain::SelectionId project_id);

    // IConfigContainerListChangedListener
    void on_config_container_list_changed(Domain::SelectionId project_id) override;
    void on_config_container_bed_list_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id
    ) override;

    void on_project_will_be_removed(Domain::SelectionId project_id) override;
    void on_project_loaded(Domain::SelectionId project_id) override;

private:
    ObjectListProjectContext& context(Domain::SelectionId project_id);
    void refresh(Domain::SelectionId project_id);

    Scene::SceneInteractor& m_scene_interactor;
    Biz::ProjectInteractor* m_project_interactor{nullptr};

    ListenerScope<IConfigContainerListChangedListener, Scene::SceneInteractor, ObjectListInteractor>
        m_config_container_list_changed_listener_scope;

    // Bound late via bind_project_interactor() (ProjectScoped needs a live ProjectInteractor&
    // at construction). Automatically creates/destroys an ObjectListProjectContext as projects
    // are opened/removed - see Biz::ProjectScoped and ObjectListOld::ProjectContexts for the
    // same pattern used elsewhere.
    std::optional<ProjectScoped<ObjectListProjectContext>> m_project_contexts;
};

} // namespace Slic3r::Biz::ObjectList
