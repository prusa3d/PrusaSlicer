#pragma once

#include <map>
#include "Slic3r/Biz/IProjectsChangedListener.hpp"
#include "Slic3r/App/Undo/Stack.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/ISelectedProjectChangedListener.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Scene {
class SceneInteractor;
} // namespace Slic3r::Biz


namespace Slic3r::App::Undo{

class IStoreChangedListener
{
public:
    virtual ~IStoreChangedListener() = default;

    virtual void on_undo_store_changed(
        Domain::SelectionId project_id,
        const std::vector<std::pair<std::size_t, std::string>>& snapshots,
        std::size_t selected_index
    ) = 0;
};

/* @brief Serializes and updates the application state on demand.
 *
 * The serialization and updates happen on per project basis.
 *
 * Internally, it takes snapshots. Snapshot is as std::vector<SnapshotData>.
 * SnapshotData is std::string (directly serialized data) and
 * std::map<ChannelId, Chunk>. Chunk is arbitrary (but defined in compile time)
 * data that will be stored in separate channel.
 *
 * Serialization:
 *   The stack keeps snapshots and N channels - each with unique id. It creates
 *   std::vector<SerializedData> and stores it in a snapshot, while putting its
 *   chunks into the appropriate channels. The channels are de-duplicated, meaning
 *   that if the same data would be stored twice, the channel simply remembers
 *   that the chunk is valid for multiple snapshots.
 *
 * Update:
 *   It aggregates the data from all channels and a snapshot, it than takes these and
 *   deserializes them. After that, it does the actual update of the application
 *   state by calling the appropriate methods.
 */
class Store final :
    public Biz::IProjectsChangedListener,
    public Biz::ISelectedProjectChangedListener,
    public Biz::IUndoProvider,
    public WithListeners<IStoreChangedListener>
{
public:
    Store(
        Biz::ProjectInteractor& project_interactor
    );
    ~Store();

    void take_snapshot(Biz::UndoSnapshotType snapshot_type) override;
    bool is_undo_possible() const override;
    bool is_redo_possible() const override;

    void select_snapshot(
        Biz::UndoSnapshotSelection::Variant snapshot_variant
    ) override;

    void on_project_loaded(Domain::SelectionId project_id) override;
    void on_project_removed(Domain::SelectionId project_id) override;
    void on_selected_project_changed_final(Domain::SelectionId project_id) override;

    const Stack& get_undo_stack(Domain::SelectionId project_id) const;

    void set_gizmo_controller(Scene::IGizmoController* gizmo_controller);

private:
    std::map<Domain::SelectionId, Stack> m_stacks;

    Biz::ProjectInteractor& m_project_interactor;
    Biz::Scene::SceneInteractor& m_scene_interactor;
    const Domain::Workbench& m_workbench;

    Scene::IGizmoController* m_gizmo_controller{nullptr};

    bool m_snapshotting_enabled{true};

    void update_top_bar(Domain::SelectionId project_id, const Stack& stack);
};
} // namespace Slic3r::App::Undo
