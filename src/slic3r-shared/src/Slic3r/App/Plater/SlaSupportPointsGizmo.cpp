#include "Slic3r/App/Plater/SlaSupportPointsGizmo.hpp"

#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ObjectSettingsInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Domain/ModelObject.hpp"

#include "libslic3r/SLAResult.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <numbers>

using Slic3r::Biz::_u8L;
using Slic3r::Biz::UndoSnapshotType;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::SLA::PointsStatus;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace Slic3r::App::Plater {

static constexpr double ConeRadius = 0.25;
static constexpr double ConeHeight = 0.75;

static const ColorRGBA GeneratedPointColor{0.75f, 0.75f, 0.75f, 1.f};
static const ColorRGBA IslandPointColor{0.f, 0.5f, 1.f, 1.f};
static const ColorRGBA ManualPointColor{1.f, 0.5f, 0.f, 1.f};

static constexpr const char* DensityKey = "support_points_density_relative";

SlaSupportPointsGizmo::SlaSupportPointsGizmo(
    Render::Device& device,
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    SlaGizmoBase(device, project_interactor, scene_presenter)
{
    m_dialog     = std::make_unique<SlaSupportPointsDialog>();
    m_dialog_ptr = m_dialog.get();
    m_dialog->callbacks().auto_generate = [this]() { this->auto_generate(); };
    m_dialog->callbacks().remove_all    = [this]() { this->remove_all(); };
    m_dialog->callbacks().apply         = [this]() { this->apply(); };
    m_dialog->callbacks().density_changed = [this](int density) { this->set_density(density); };
    m_dialog->callbacks().head_diameter_changed = [this](double head_diameter)
    { this->set_head_diameter(head_diameter); };
    m_dialog->callbacks().lock_islands_changed = [this](bool lock) { m_lock_islands = lock; };
    m_dialog->callbacks().clipping_of_view_value_changed = [this](double value)
    { this->set_clipping_position(value); };
    m_dialog->callbacks().clipping_of_view_reset_direction = [this]()
    { this->reset_clipping_direction(); };
}

SlaSupportPointsGizmo::~SlaSupportPointsGizmo() = default;

Scene::ToolType SlaSupportPointsGizmo::type() const
{
    return Scene::ToolType::SlaSupportPointsGizmo;
}

GizmoWindowPtr SlaSupportPointsGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

std::optional<Undo::ToolState> SlaSupportPointsGizmo::get_tool_state() const
{
    // Only while active: undo can't restore the state of a tool of another printer technology.
    if (!this->active()) {
        return std::nullopt;
    }
    // Applied points belong to the model (user modified) or to the backend (generated). Only the
    // unapplied ones are the gizmo's own state, the others are taken from where they are.
    return Undo::SlaSupportPointsGizmoState{
        this->object_id(),
        this->dirty() ? m_points : std::vector<Point>{},
        this->dirty()
    };
}

void SlaSupportPointsGizmo::set_tool_state(const Undo::ToolState& tool_state)
{
    ASSERT(std::holds_alternative<Undo::SlaSupportPointsGizmoState>(tool_state));
    const auto& state = std::get<Undo::SlaSupportPointsGizmoState>(tool_state);
    if (!this->active() || state.object_id != this->object_id()) {
        return;
    }

    if (state.dirty) {
        m_points = state.points;
    }
    this->restore_items(state.dirty);
}

size_t SlaSupportPointsGizmo::item_count() const
{
    return m_points.size();
}

bool SlaSupportPointsGizmo::item_selected(const size_t index) const
{
    return m_points[index].selected;
}

void SlaSupportPointsGizmo::set_item_selected(const size_t index, const bool selected)
{
    m_points[index].selected = selected;
}

Vec3d SlaSupportPointsGizmo::item_position(const size_t index) const
{
    return m_points[index].support_point.pos.cast<double>();
}

Vec3d SlaSupportPointsGizmo::item_out_direction(const size_t index) const
{
    return this->surface_world_normal(this->item_position(index));
}

void SlaSupportPointsGizmo::add_item(const MeshHit& hit)
{
    m_points.push_back(Point{
        SupportPoint{
            hit.position.cast<float>(),
            float(m_head_diameter / 2.),
            SupportPointType::manual_add
        },
        false
    });
}

bool SlaSupportPointsGizmo::move_item(const size_t index, const MeshHit& hit)
{
    SupportPoint& point = m_points[index].support_point;
    if (m_lock_islands && point.is_island()) {
        return false;
    }

    point.pos  = hit.position.cast<float>();
    point.type = SupportPointType::manual_add;
    return true;
}

size_t SlaSupportPointsGizmo::erase_selected_items(const bool force)
{
    return std::erase_if(
        m_points,
        [this, force](const Point& point)
        { return point.selected && (force || !m_lock_islands || !point.support_point.is_island()); }
    );
}

void SlaSupportPointsGizmo::clear_items()
{
    m_points.clear();
}

void SlaSupportPointsGizmo::load_items(const Biz::Slicing::Sla::Object* backend_object)
{
    const ModelObject* mo = this->model_object();
    m_points.clear();
    if (mo != nullptr && mo->sla_points_status == PointsStatus::UserModified) {
        for (const SupportPoint& point : mo->sla_support_points) {
            m_points.push_back(Point{point, false});
        }
    } else if (backend_object != nullptr && backend_object->support_points) {
        const Transform3d to_model = backend_object->object_trafo.inverse();
        for (SupportPoint point : *backend_object->support_points) {
            point.pos = (to_model * point.pos.cast<double>()).cast<float>();
            m_points.push_back(Point{point, false});
        }
    }
}

void SlaSupportPointsGizmo::write_items(ModelObject& model_object) const
{
    model_object.sla_support_points.clear();
    model_object.sla_support_points.reserve(m_points.size());
    for (const Point& point : m_points) {
        model_object.sla_support_points.push_back(point.support_point);
    }
    model_object.sla_points_status = PointsStatus::UserModified;
}

void SlaSupportPointsGizmo::remember_item(const size_t index)
{
    m_point_before_drag = m_points[index];
}

bool SlaSupportPointsGizmo::item_changed(const size_t index) const
{
    return m_point_before_drag.has_value()
        && m_points[index].support_point.pos != m_point_before_drag->support_point.pos;
}

std::vector<SlaGizmoBase::ItemMesh> SlaSupportPointsGizmo::item_meshes() const
{
    return {
        ItemMesh{m_sphere_geometry.get(), m_sphere_trimesh.get()},
        ItemMesh{m_cone_geometry.get(), m_cone_trimesh.get()}
    };
}

std::vector<std::optional<Transform3d>> SlaSupportPointsGizmo::item_mesh_trafos(const size_t index
) const
{
    const SupportPoint& point = m_points[index].support_point;
    const Vec3d position      = this->items_to_world() * point.pos.cast<double>();
    const double radius       = point.head_front_radius;

    Transform3d sphere_trafo = Transform3d::Identity();
    sphere_trafo.translate(position);
    sphere_trafo.scale(radius);

    // The cone points at the sphere from outside the surface, it needs the backend mesh.
    const Vec3d normal = this->item_out_direction(index);
    if (normal.isZero()) {
        return {sphere_trafo, std::nullopt};
    }

    Transform3d cone_trafo = Transform3d::Identity();
    cone_trafo.translate(position);
    cone_trafo.rotate(Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), normal));
    cone_trafo.translate((ConeHeight + radius) * Vec3d::UnitZ());
    cone_trafo.rotate(Eigen::AngleAxisd(std::numbers::pi, Vec3d::UnitX()));
    cone_trafo.scale(Vec3d{ConeRadius, ConeRadius, ConeHeight});
    return {sphere_trafo, cone_trafo};
}

ColorRGBA SlaSupportPointsGizmo::item_color(const size_t index) const
{
    switch (m_points[index].support_point.type) {
    case SupportPointType::manual_add: return ManualPointColor;
    case SupportPointType::island: return IslandPointColor;
    default: return GeneratedPointColor;
    }
}

void SlaSupportPointsGizmo::on_item_selected(const size_t index)
{
    m_head_diameter = 2. * m_points[index].support_point.head_front_radius;
}

SLAPrintObjectStep SlaSupportPointsGizmo::step_to_show() const
{
    // Generated points are shown only after the user asked for them, as in the old gizmo. Then
    // they have to be there whenever the gizmo shows the object (e.g. after undo/redo).
    const ModelObject* mo = this->model_object();
    const bool generated  = mo != nullptr && mo->sla_points_status == PointsStatus::AutoGenerated;
    return generated ? slaposSupportPoints : slaposDrillHoles;
}

SLAPrintObjectStep SlaSupportPointsGizmo::step_after_apply() const
{
    return slaposSupportPoints;
}

UndoSnapshotType SlaSupportPointsGizmo::snapshot_type(const Edit edit) const
{
    switch (edit) {
    case Edit::Add: return UndoSnapshotType::SlaSupportPointsAdd;
    case Edit::Delete: return UndoSnapshotType::SlaSupportPointsDelete;
    case Edit::Move: return UndoSnapshotType::SlaSupportPointsMove;
    case Edit::RemoveAll: return UndoSnapshotType::SlaSupportPointsRemoveAll;
    case Edit::Apply: return UndoSnapshotType::SlaSupportPointsApply;
    }
    return UndoSnapshotType::SlaSupportPointsApply;
}

const char* SlaSupportPointsGizmo::command_prefix() const
{
    return "sla-support-points-gizmo";
}

void SlaSupportPointsGizmo::on_target_activated()
{
    if (m_sphere_trimesh == nullptr) {
        m_sphere_trimesh = std::make_unique<Scene::TriangleMesh>(
            TriMesh::its_make_sphere(1., std::numbers::pi / 12.)
        );
        m_sphere_geometry = Render::geometry_from_triangle_mesh(m_device, m_sphere_trimesh->triangles());
        m_cone_trimesh =
            std::make_unique<Scene::TriangleMesh>(TriMesh::its_make_cone(1., 1., std::numbers::pi / 12.));
        m_cone_geometry = Render::geometry_from_triangle_mesh(m_device, m_cone_trimesh->triangles());
    }

    const Domain::ConfigPack config =
        m_project_interactor.selected_config_container().build_print_config();
    if (const auto* sla = std::get_if<Domain::ConfigPackSLA>(&config)) {
        m_head_diameter =
            sla->sla_print_settings.items.opt("support_head_front_diameter").get<double>();
    }
}

void SlaSupportPointsGizmo::reset_dialog_clipping()
{
    m_dialog_ptr->set_clipping_of_view_value(0.);
}

void SlaSupportPointsGizmo::update_dialog()
{
    if (!this->active()) {
        return;
    }

    std::string status;
    if (!this->has_slicing_target()) {
        status = this->common_status();
    } else if (this->requested_step() == slaposSupportPoints) {
        status = _u8L("Generating support points...");
    } else if (!this->input_enabled()) {
        status = this->common_status();
    } else if (m_points.empty()) {
        status = _u8L("No support points.");
    } else {
        const size_t islands = std::ranges::count_if(
            m_points,
            [](const Point& point) { return point.support_point.is_island(); }
        );
        const size_t manual = std::ranges::count_if(
            m_points,
            [](const Point& point)
            { return point.support_point.type == SupportPointType::manual_add; }
        );
        const size_t count = m_points.size();
        status             = fmt::vformat(
            _u8L("{} support points ({} manual, {} on islands)"),
            fmt::make_format_args(count, manual, islands)
        );
    }
    if (this->dirty()) {
        status += "\n" + _u8L("The changes are not applied yet.");
    }
    m_dialog_ptr->set_status(status);

    m_dialog_ptr->set_input_enabled(this->input_enabled());
    m_dialog_ptr->set_apply_enabled(this->dirty());
    m_dialog_ptr->set_remove_all_enabled(!m_points.empty());
    m_dialog_ptr->set_lock_islands(m_lock_islands);

    double max_head_diameter = m_head_diameter;
    const Domain::ConfigPack config =
        m_project_interactor.selected_config_container().build_print_config();
    if (const auto* sla = std::get_if<Domain::ConfigPackSLA>(&config)) {
        max_head_diameter =
            sla->sla_print_settings.items.opt("support_pillar_diameter").get<double>();
    }
    m_dialog_ptr->set_head_diameter(m_head_diameter, max_head_diameter);

    const auto list = m_project_interactor.preset_interactor()
                          .object_settings_interactor()
                          .object_observable_list()
                          .lock();
    if (list != nullptr) {
        for (size_t i = 0; i < list->size(); ++i) {
            const Biz::OverrideItem& item = list->at(i);
            if (item.name == DensityKey && item.config_item != nullptr) {
                m_dialog_ptr->set_density(item.config_item->get<int>());
            }
        }
    }
}

void SlaSupportPointsGizmo::set_head_diameter(const double head_diameter)
{
    m_head_diameter = head_diameter;

    bool changed = false;
    for (size_t index = 0; index < m_points.size(); ++index) {
        if (m_points[index].selected) {
            m_points[index].support_point.head_front_radius = float(head_diameter / 2.);
            this->update_item_node(index);
            changed = true;
        }
    }

    if (changed) {
        this->edited(UndoSnapshotType::SlaSupportPointsHeadDiameter);
    }
}

void SlaSupportPointsGizmo::set_density(const int density)
{
    const auto list = m_project_interactor.preset_interactor()
                          .object_settings_interactor()
                          .object_observable_list()
                          .lock();
    if (list == nullptr) {
        return;
    }

    for (size_t i = 0; i < list->size(); ++i) {
        const Biz::OverrideItem& item = list->at(i);
        if (item.name == DensityKey && item.config_item != nullptr) {
            m_project_interactor.preset_interactor()
                .set_item_value(*item.config_item, Domain::ConfigValue{density}, {0});
            // As in the old gizmo: a new density means new points.
            this->auto_generate();
            return;
        }
    }
}

void SlaSupportPointsGizmo::auto_generate()
{
    const ModelObject* mo = this->model_object();
    if (mo == nullptr) {
        return;
    }

    const bool user_points =
        mo->sla_points_status == PointsStatus::UserModified || !mo->sla_support_points.empty();
    const bool was_dirty = this->dirty();
    this->discard_edits();

    if (user_points) {
        m_scene_interactor.modify_sla_object_data(
            Domain::ElementRef{this->object_id()},
            [](ModelObject& model_object)
            {
                model_object.sla_support_points.clear();
                model_object.sla_points_status = PointsStatus::AutoGenerated;
            }
        );
    }

    this->load_backend_object();
    if (user_points || was_dirty) {
        m_project_interactor.undo_provider().take_snapshot(
            UndoSnapshotType::SlaSupportPointsAutoGenerate
        );
    }
    // The model change reaches the object cache later, don't take the old points from it.
    this->request_backend(slaposSupportPoints, false);
}

} // namespace Slic3r::App::Plater
