#include "Slic3r/App/Plater/QuickDragGizmo.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

#include <tracy/Tracy.hpp>

using Slic3r::Domain::SquareMatrix4d;
using Slic3r::Domain::Vec3d;
using Slic3r::App::Scene::SceneNodeTag;

namespace {
constexpr std::chrono::milliseconds VIRTUAL_BED_DELAY{500};
}

namespace Slic3r::App::Plater {
QuickDragGizmo::QuickDragGizmo(
    Biz::ProjectInteractor& project_interactor,
    Scene::ISceneProvider& scene_provider
) :
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_scene_provider(scene_provider),
    m_selection_handler(project_interactor.scene_interactor())
{}

Scene::GizmoActivationState QuickDragGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    return Scene::GizmoActivationState::Inactive;
}

bool QuickDragGizmo::on_drag_start(const Scene::GizmoEventContext& ctx)
{
    ZoneScoped;

    // Additional condition to start dragging by QuickDrag
    const Scene::NodePickResult* n{nullptr};
    if (ctx.mouse_event().key_modifiers() != 0
        || (n = ctx.pick_result_with_tag_of_type<SceneNodeTag>()) == nullptr)
        return false;

    // Move the plane at same z-level as the node hit point is
    m_plane.d = -ctx.pick_ray().point_at(n->cast.distance).z();

    if (!mouse_pos(ctx.screen_mouse_x(), ctx.screen_mouse_y(), m_initial_world_pos))
        return false;

    if (!can_be_added_to_object_selection(*n->node, m_scene_interactor.object_selection()))
        return false;

    const auto& selection = m_scene_interactor.object_selection();

    const SceneNodeTag* tag = n->node->tag_of_type<SceneNodeTag>();
    bool already_selected = tag
        && (selection.is_selected({ tag->object_id, tag->instance_id, tag->volume_id })
            || selection.is_selected({ tag->object_id, tag->instance_id, 0 }));
    if (!already_selected) {
        m_selection_handler.mark_selected(*n->node, true, true);
    }

    m_was_off_bed = false;
    m_virtual_bed_timer_id = Biz::Platform::TimerQueue::TimerID{};
    m_dragging = true;
    return true;
}

bool QuickDragGizmo::on_dragging(const Scene::GizmoEventContext& ctx)
{
    ZoneScoped;
    Vec3d p;
    if (!mouse_pos(ctx.screen_mouse_x(), ctx.screen_mouse_y(), p)) {
        // weird should not appear
        return false;
    }

    SquareMatrix4d xform    = SquareMatrix4d::Identity();
    xform.block<3, 1>(0, 3) = p - m_initial_world_pos;

    m_scene_interactor.transform_selection(xform, m_xform_memento);

    const bool off_bed = selection_is_off_bed();
    if (off_bed && !m_was_off_bed) {
        // Debounce the virtual-bed preview: require the selection to stay off-bed for a short while.
        const Domain::SelectionId cc_id = m_scene_interactor.selected_config_container_id();
        ASSERT(m_virtual_bed_timer_id == Biz::Platform::TimerQueue::TimerID{});
        m_virtual_bed_timer_id = Biz::Platform::PlatformServices::instance().timer_queue().set_timer(
            VIRTUAL_BED_DELAY,
            [this, cc_id]() {
                if (m_dragging && selection_is_off_bed())
                    m_scene_interactor.show_virtual_bed_preview(cc_id);
                m_virtual_bed_timer_id = Biz::Platform::TimerQueue::TimerID{};
            },
            false
        );
    }
    else if (!off_bed && m_was_off_bed) {
        cancel_virtual_bed_timer();
        if (m_scene_interactor.virtual_bed_preview().has_value())
            m_scene_interactor.hide_virtual_bed_preview();
    }
    m_was_off_bed = off_bed;
    return true;
}

void QuickDragGizmo::on_drag_finish()
{
    m_dragging = false;
    cancel_virtual_bed_timer();

    const bool commit_virtual_bed =
        m_scene_interactor.virtual_bed_preview().has_value()
        && m_scene_interactor.virtual_bed_preview_accepts_selection();
    const Domain::SelectionId cc_id =
        commit_virtual_bed ? m_scene_interactor.virtual_bed_preview()->config_container_id
                           : Domain::INVALID_ID;

    m_scene_interactor.hide_virtual_bed_preview();

    m_scene_interactor.finalize_transform_selection(m_xform_memento, false);

    if (commit_virtual_bed) {
        const Domain::BedInstance& bed_inst = m_scene_interactor.add_bed_instance(cc_id);
        m_scene_interactor.bed_selection().select_one(
            Domain::BedRef(cc_id, bed_inst.id().id),
            Biz::Scene::CameraActionOnBedSelection::CenterOnBed
        );
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::QuickDragAndAddBed);
    } else {
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::QuickDrag);
    }
    m_was_off_bed = false;
}

void QuickDragGizmo::on_drag_cancel()
{
    m_dragging = false;
    cancel_virtual_bed_timer();
    if (m_scene_interactor.virtual_bed_preview().has_value())
        m_scene_interactor.hide_virtual_bed_preview();
    m_was_off_bed = false;
    m_scene_interactor.finalize_transform_selection(m_xform_memento, true);
}

bool QuickDragGizmo::selection_is_off_bed() const
{
    // Returns true only when all volumes of an unplaced instance are selected.
    // Partially selected instances are ignored so that dragging a single part
    // does not trigger the virtual bed suggestion.
    const auto& unplaced = m_scene_interactor.selected_project_unplaced_model_instances();
    if (unplaced.empty())
        return false;
    const auto& selection = m_scene_interactor.object_selection();

    using InstKey = std::pair<size_t, size_t>;
    std::map<InstKey, size_t> sel_volume_counts;
    std::set<InstKey> fully_selected;

    // Count how many volumes are selected for each instance, and track fully selected instances.
    for (const Domain::ElementRef& e : selection.elements) {
        if (!e.has_instance())
            continue;
        const InstKey key{e.object_id, e.instance_id};
        if (!e.has_volume())
            fully_selected.insert(key);
        else
            ++sel_volume_counts[key];
    }

    // Check if any unplaced instance is fully selected.
    for (const Domain::ModelInstance* mi : unplaced) {
        if (mi == nullptr)
            continue;
        const Domain::ModelObject* mo = mi->get_object();
        const InstKey key{mo->id().id, mi->id().id};
        if (fully_selected.count(key))
            return true;
        const auto it = sel_volume_counts.find(key);
        if (it != sel_volume_counts.end() && it->second == mo->volumes.size())
            return true;
    }
    return false;
}

void QuickDragGizmo::cancel_virtual_bed_timer()
{
    if (Biz::Platform::PlatformServices::instance().timer_queue().is_timer_running(m_virtual_bed_timer_id)) {
        Biz::Platform::PlatformServices::instance().timer_queue().cancel_timer(m_virtual_bed_timer_id);
    }
    m_virtual_bed_timer_id = Biz::Platform::TimerQueue::TimerID{};
}

bool QuickDragGizmo::mouse_pos(float screen_x, float screen_y, Vec3d& out_pos)
{
    const auto& cam = m_scene_provider.scene().camera();
    const auto ray  = cam.ray_at(screen_x, screen_y);
    double t;
    if (m_plane.intersects(ray, t)) {
        out_pos = ray.point_at(t);
        return true;
    }
    return false;
}

} // namespace Slic3r::App::Plater
