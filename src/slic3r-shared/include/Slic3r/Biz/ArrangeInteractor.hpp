#pragma once

#include "Slic3r/Biz/Arrange/Arrange.hpp"
#include "Slic3r/Biz/Arrange/Settings.hpp"
#include "Slic3r/Biz/IArrangeEventsListener.hpp"
#include "Slic3r/Biz/IUndoProvider.hpp"
#include "Slic3r/Biz/Scene/SceneInteractor.hpp"

#include <functional>
#include <queue>
#include <set>

namespace Slic3r::Biz {

struct ArrangeBed
{
    const Domain::BedInstance* bed_instance;
    double offset{};
};

using ArrangeBeds = std::vector<ArrangeBed>;

struct Pack
{
    Arrange::InstanceTransforms trafos;
    Domain::BoundingBox2d bounding_box;
};

struct BedToArrange {
    Domain::BedRef ref;
    std::size_t index{};
    Domain::ConstModelInstanceList arrangeable;
    Domain::ConstModelInstanceList fixed;
    bool fixed_wipe_tower{false};
};

using Packs = std::vector<Pack>;

class ArrangeInteractor : public WithListeners<IArrangeEventsListener>
{
public:
    ArrangeInteractor(Scene::SceneInteractor& scene_interactor, const Domain::Workbench& workbench);

    void arrange(
        const Domain::SelectionId project_id,
        const std::vector<BedToArrange>& beds,
        std::optional<std::size_t> config_container_to_add_beds,
        const Domain::ConstModelInstanceList& extra,
        const Arrange::Settings& settings,
        std::function<void()> on_finished,
        const std::string& job_name = "arrange"
    );

    /**
     * @brief Auto-arranges freshly added instances on the target bed, keeping every other
     *        instance fixed, and takes a single undo snapshot once the operation finished.
     *
     * @param project_id ID of the target project.
     * @param added_instances Instance-level element refs of the newly added items to arrange.
     * @param target_bed Bed to arrange the new instances on.
     * @param snapshot_type Undo the snapshot type.
     */
    void arrange_added_instances(
        Domain::SelectionId project_id,
        const Domain::ElementRefs& added_instances,
        const Domain::BedRef& target_bed,
        UndoSnapshotType snapshot_type
    );

private:
    Scene::SceneInteractor& m_scene_interactor;
    const Domain::Workbench& m_workbench;

    struct PendingArrange
    {
        Domain::SelectionId project_id{Domain::INVALID_ID};
        std::set<size_t> instance_ids;
        Domain::BedRef target_bed;
        UndoSnapshotType snapshot_type{};
    };

    std::queue<PendingArrange> m_added_arrange_queue;

    void process_added_arrange_queue();

    void apply_local_arrange_result(
        const Domain::SelectionId project_id,
        const std::vector<std::pair<Domain::BedRef, Pack>>& packs,
        const Packs& unpacked_packs,
        std::optional<std::size_t> config_container_to_add_beds);
};
} // namespace Slic3r::Biz
