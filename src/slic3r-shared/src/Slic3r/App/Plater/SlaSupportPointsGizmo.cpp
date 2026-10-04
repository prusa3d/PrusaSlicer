#include "Slic3r/App/Plater/SlaSupportPointsGizmo.hpp"

#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Plater/SlaSupportPointsDialog.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Render/ScreenInfo.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ObjectSettingsInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SLAObjectRequest.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include "libslic3r/SLAResult.hpp"

#include <fmt/format.h>
#include <magic_enum/magic_enum_flags.hpp>

#include <algorithm>
#include <array>
#include <numbers>

using namespace magic_enum::bitwise_operators;

using Slic3r::App::Scene::SceneNodeTag;
using Slic3r::Biz::_u8L;
using Slic3r::Biz::SLAObjectOptRef;
using Slic3r::Biz::UndoSnapshotType;
using Slic3r::Biz::Utils::MeshRaycaster::get_closest_facet;
using Slic3r::Biz::Utils::MeshRaycaster::unproject_on_mesh;
using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::ModelInstance;
using Slic3r::Domain::ModelObject;
using Slic3r::Domain::ModelVolume;
using Slic3r::Domain::PrinterTechnology;
using Slic3r::Domain::Project;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;
using Slic3r::Domain::SLA::PointsStatus;
using Slic3r::Domain::SLA::SupportPoint;
using Slic3r::Domain::SLA::SupportPointType;

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace Slic3r::App::Plater {

static constexpr double ConeRadius = 0.25;
static constexpr double ConeHeight = 0.75;
// A point is visible from the camera if this point above the surface along its normal is.
static constexpr double VisibilityOffset = 0.2;

static const ColorRGBA BackendMeshColor{0.85f, 0.85f, 0.85f, 1.f};
static const ColorRGBA DisabledMeshColor{0.35f, 0.35f, 0.35f, 1.f};
static const ColorRGBA GeneratedPointColor{0.75f, 0.75f, 0.75f, 1.f};
static const ColorRGBA IslandPointColor{0.f, 0.5f, 1.f, 1.f};
static const ColorRGBA ManualPointColor{1.f, 0.5f, 0.f, 1.f};
static const ColorRGBA SelectedPointColor{1.f, 0.f, 0.25f, 1.f};
static const ColorRGBA HoveredPointColor{0.f, 1.f, 1.f, 1.f};

SlaSupportPointsGizmo::SlaSupportPointsGizmo(
    Render::Device& device,
    Biz::ProjectInteractor& project_interactor,
    PlaterScenePresenter& scene_presenter
) :
    m_device(device),
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_scene_presenter(scene_presenter)
{
    m_clipper_presenter = Scene::ClipperPresenter(&m_clipper, &m_device, &m_scene_presenter);

    m_request = std::make_unique<Biz::SLAObjectRequest>(
        project_interactor.slicing_interactor(),
        project_interactor.status_cache(),
        project_interactor.sla_object_cache()
    );
    m_request->callbacks().completed = [this](SLAObjectOptRef)
    {
        m_requested_step.reset();
        this->load_backend_object();
    };

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
    {
        m_clipper_presenter.set_position_by_ratio(value, true);
        this->update_clipping();
    };
    m_dialog->callbacks().clipping_of_view_reset_direction = [this]()
    {
        m_clipper_presenter.set_position_by_ratio(-1., false);
        this->update_clipping();
    };

    m_project_interactor.sla_object_cache().add_listener<Biz::ISLAObjectCacheChangedListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

SlaSupportPointsGizmo::~SlaSupportPointsGizmo()
{
    m_project_interactor.sla_object_cache().remove_listener<Biz::ISLAObjectCacheChangedListener>(
        this
    );
    m_scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

Scene::ToolType SlaSupportPointsGizmo::type() const
{
    return Scene::ToolType::SlaSupportPointsGizmo;
}

bool SlaSupportPointsGizmo::supports_printer(const PrinterTechnology pt) const
{
    return pt == PrinterTechnology::SLA;
}

bool SlaSupportPointsGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection = m_scene_interactor.object_selection();
    if (selection.state() != Biz::Scene::SelectionState::WholeInstance
        || selection.elements.size() != 1)
    {
        return false;
    }

    const Domain::ConfigContainer* config_container =
        m_project_interactor.selected_project()
            .find_config_container(m_project_interactor.selected_config_container_id());
    return config_container != nullptr
        && config_container->print_technology() == PrinterTechnology::SLA;
}

bool SlaSupportPointsGizmo::disable_object_selection() const
{
    return true;
}

void SlaSupportPointsGizmo::provide_gizmo_controller(Scene::IGizmoController& controller)
{
    m_gizmo_controller = &controller;
}

GizmoWindowPtr SlaSupportPointsGizmo::release_ui_window()
{
    return std::move(m_dialog);
}

std::optional<Undo::ToolState> SlaSupportPointsGizmo::get_tool_state() const
{
    // Only while active: undo can't restore the state of a tool of another printer technology.
    if (!m_active) {
        return std::nullopt;
    }
    // Applied points belong to the model (user modified) or to the backend (generated). Only the
    // unapplied ones are the gizmo's own state, the others are taken from where they are.
    return Undo::SlaSupportPointsGizmoState{
        m_object_id,
        m_dirty ? m_points : std::vector<Point>{},
        m_dirty
    };
}

void SlaSupportPointsGizmo::set_tool_state(const Undo::ToolState& tool_state)
{
    ASSERT(std::holds_alternative<Undo::SlaSupportPointsGizmoState>(tool_state));
    const auto& state = std::get<Undo::SlaSupportPointsGizmoState>(tool_state);
    if (!m_active || state.object_id != m_object_id) {
        return;
    }

    m_hovered.reset();
    m_dirty = state.dirty;
    if (m_dirty) {
        m_points = state.points;
        this->rebuild_point_nodes();
        this->update_dialog();
    } else {
        this->load_backend_object();
        // The restored model reaches the object cache later, don't take the old points from it.
        this->request_backend_for_model(false);
    }
}

void SlaSupportPointsGizmo::on_activated()
{
    if (!this->find_target()) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    m_active = true;
    this->init_scene();

    const Domain::ConfigPack config =
        m_project_interactor.selected_config_container().build_print_config();
    if (const auto* sla = std::get_if<Domain::ConfigPackSLA>(&config)) {
        m_head_diameter =
            sla->sla_print_settings.items.opt("support_head_front_diameter").get<double>();
    }

    this->load_backend_object();

    // Points the user didn't apply before switching to another project.
    const auto saved{m_saved_caches.find(m_project_interactor.selected_project_id())};
    if (saved != m_saved_caches.end()) {
        if (saved->second.object_id == m_object_id && saved->second.instance_id == m_instance_id) {
            m_points = std::move(saved->second.points);
            m_dirty  = true;
            this->rebuild_point_nodes();
        }
        m_saved_caches.erase(saved);
    }

    this->request_backend_for_model(true);
    this->update_dialog();
}

void SlaSupportPointsGizmo::on_deactivated()
{
    if (!m_active) {
        return;
    }

    // Undo/redo restores the model and the points together, nothing to write.
    if (!m_project_interactor.undo_provider().is_restoring_snapshot()) {
        this->flush();
    }

    this->release_scene();
    this->reset_target();
}

void SlaSupportPointsGizmo::on_project_activated(size_t)
{
    this->on_activated();
}

void SlaSupportPointsGizmo::on_project_deactivated(const size_t old_project_id)
{
    if (!m_active) {
        return;
    }

    // The selected project has already changed, so the points can't be written into the model.
    // Keep them until the project is selected again.
    if (m_dirty) {
        m_saved_caches[old_project_id] = SavedCache{m_object_id, m_instance_id, m_points};
    }

    this->release_scene();
    this->reset_target();
}

bool SlaSupportPointsGizmo::find_target()
{
    const Biz::Scene::ObjectSelection& selection = m_scene_interactor.object_selection();
    if (selection.elements.size() != 1
        || selection.mode != Biz::Scene::SelectionMode::Instance
        || selection.elements.front().is_wipe_tower())
    {
        return false;
    }

    const Domain::ElementRef& element = selection.elements.front();
    const Project& project            = m_project_interactor.selected_project();
    const ModelInstance* instance = project.find_instance_by_id(element.object_id, element.instance_id);
    if (instance == nullptr) {
        return false;
    }

    m_object_id   = element.object_id;
    m_instance_id = element.instance_id;

    m_slicing_id.reset();
    m_bed_trafo                       = Transform3d::Identity();
    const Domain::BedRef& bed_ref     = instance->get_last_bed();
    const Domain::BedInstance* bed    = project.find_bed_instance_by_id(bed_ref.instance_id);
    if (bed != nullptr && instance->is_printable()) {
        m_slicing_id = Domain::SlicingId{m_project_interactor.selected_project_id(), bed_ref.instance_id};
        m_bed_trafo  = bed->transformation.get_matrix();
    }
    return true;
}

void SlaSupportPointsGizmo::reset_target()
{
    m_request->cancel();
    m_requested_step.reset();

    m_active = false;
    m_slicing_id.reset();
    m_points.clear();
    m_dirty = false;

    m_backend_mesh.reset();
    m_backend_trimesh.reset();
    m_backend_geometry.reset();
    m_model_geometries.clear();

    m_hovered.reset();
    m_dragged.reset();
    m_point_before_drag.reset();
    m_wait_for_up = false;
    m_rectangle.reset();
}

const ModelObject* SlaSupportPointsGizmo::model_object() const
{
    return m_project_interactor.selected_project().find_object_by_id(m_object_id);
}

const ModelInstance* SlaSupportPointsGizmo::model_instance() const
{
    return m_project_interactor.selected_project().find_instance_by_id(m_object_id, m_instance_id);
}

bool SlaSupportPointsGizmo::input_enabled() const
{
    return m_active && m_backend_trimesh != nullptr;
}

void SlaSupportPointsGizmo::init_scene()
{
    m_scene = &m_scene_presenter.scene();

    if (m_sphere_trimesh == nullptr) {
        m_sphere_trimesh = std::make_unique<Scene::TriangleMesh>(
            TriMesh::its_make_sphere(1., std::numbers::pi / 12.)
        );
        m_sphere_geometry = Render::geometry_from_triangle_mesh(m_device, m_sphere_trimesh->triangles());
        m_cone_trimesh =
            std::make_unique<Scene::TriangleMesh>(TriMesh::its_make_cone(1., 1., std::numbers::pi / 12.));
        m_cone_geometry = Render::geometry_from_triangle_mesh(m_device, m_cone_trimesh->triangles());
    }

    Scene::NodeBuilder builder{*m_scene};
    builder.set_debug_name("SlaSupportPointsGizmo - main node");
    m_scene->add_child(builder.build().release(), &m_scene->root());
    m_main_node = m_scene->root().children().back().get();

    for (Scene::Node** node : {&m_mesh_node, &m_points_node, &m_clipper_node}) {
        Scene::NodeBuilder child_builder{*m_scene};
        child_builder.set_debug_name("SlaSupportPointsGizmo - child node");
        m_scene->add_child(child_builder.build().release(), m_main_node);
        *node = m_main_node->children().back().get();
    }

    this->hide_plater_nodes();
    m_scene->add_listener<Scene::ISceneChangedListener>(this);
    m_scene->add_listener<Scene::IThumbnailRenderListener>(this);

    m_elevation = 0.;
    this->activate_clipper();
    m_clipper_presenter.set_position_by_ratio(0., false);
    m_dialog_ptr->set_clipping_of_view_value(0.);
}

void SlaSupportPointsGizmo::activate_clipper()
{
    // The clipper cuts the model meshes, they have to be elevated as the backend object is.
    const double position = m_clipper.get_position();
    m_clipper_presenter.activate(
        this->model_object(),
        this->model_instance(),
        m_clipper_node,
        m_elevation,
        Scene::BuildMeshesNodes::No
    );
    m_clipper_presenter.set_behavior(true, true, 0.);
    m_clipper_presenter.set_position_by_ratio(position, true);
}

void SlaSupportPointsGizmo::release_scene()
{
    if (m_scene == nullptr) {
        return;
    }

    this->restore_plater_nodes();
    m_scene->remove_listener<Scene::ISceneChangedListener>(this);
    m_scene->remove_listener<Scene::IThumbnailRenderListener>(this);

    m_clipper_presenter.reset();
    if (m_main_node != nullptr) {
        m_scene->remove_child(m_main_node);
    }

    m_main_node    = nullptr;
    m_mesh_node    = nullptr;
    m_points_node  = nullptr;
    m_clipper_node = nullptr;
    m_point_nodes.clear();
    m_scene = nullptr;
}

void SlaSupportPointsGizmo::hide_plater_nodes()
{
    m_hidden_nodes.clear();
    m_scene->root().query(
        [](const Scene::Node* n) { return n->tag_of_type<SceneNodeTag>() != nullptr; },
        m_hidden_nodes,
        false
    );
    for (Scene::Node* node : m_hidden_nodes) {
        node->set_enabled(false);
    }
}

void SlaSupportPointsGizmo::restore_plater_nodes()
{
    for (Scene::Node* node : m_hidden_nodes) {
        node->set_enabled(true);
    }
    m_hidden_nodes.clear();
}

void SlaSupportPointsGizmo::on_node_added(Scene::Node* node)
{
    // E.g. undo recreates the nodes of the model.
    if (node->tag_of_type<SceneNodeTag>() != nullptr) {
        m_hidden_nodes.push_back(node);
        node->set_enabled(false);
    }
}

void SlaSupportPointsGizmo::on_node_removed(Scene::Node* node)
{
    std::erase(m_hidden_nodes, node);
}

void SlaSupportPointsGizmo::on_thumbnail_render_begin()
{
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(false);
    }
    for (Scene::Node* node : m_hidden_nodes) {
        node->set_enabled(true);
    }
}

void SlaSupportPointsGizmo::on_thumbnail_render_end()
{
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(true);
    }
    for (Scene::Node* node : m_hidden_nodes) {
        node->set_enabled(false);
    }
}

void SlaSupportPointsGizmo::on_model_reloaded(const Domain::SelectionId project_id)
{
    if (!m_active || project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    // E.g. undo/redo replaced the model. The points come from the restored tool state, if any.
    const ModelInstance* instance = this->model_instance();
    if (instance == nullptr) {
        // The object is gone, there is nothing to write the points to.
        m_dirty = false;
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    // The clipper refers to the replaced model object.
    this->activate_clipper();
    if (!m_dirty) {
        this->load_backend_object();
    }
}

void SlaSupportPointsGizmo::on_scene_selection_changed(
    const Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    if (!m_active || project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    if (selection.elements.size() == 1 && selection.elements.front().object_id == m_object_id
        && selection.elements.front().instance_id == m_instance_id)
    {
        return;
    }

    // Another object: start over with it, keeping what was edited on the old one.
    if (!m_project_interactor.undo_provider().is_restoring_snapshot()) {
        this->flush();
    }
    this->release_scene();
    this->reset_target();
    this->on_activated();
}

void SlaSupportPointsGizmo::on_sla_object_cache_changed(
    const Domain::SlicingId& id,
    const Domain::ObjectID object_id
)
{
    if (m_active && m_slicing_id == id && object_id.id == m_object_id) {
        this->load_backend_object();
    }
}

void SlaSupportPointsGizmo::request_backend(const SLAPrintObjectStep step, const bool use_cache)
{
    if (!m_slicing_id.has_value()) {
        return;
    }

    m_requested_step = step;
    m_request->start(*m_slicing_id, Domain::ObjectID{m_object_id}, step, use_cache);
    this->update_dialog();
}

void SlaSupportPointsGizmo::request_backend_for_model(const bool use_cache)
{
    // Generated points are shown only after the user asked for them, as in the old gizmo. Then
    // they have to be there whenever the gizmo shows the object (e.g. after undo/redo).
    const ModelObject* mo = this->model_object();
    const bool generated  = mo != nullptr && mo->sla_points_status == PointsStatus::AutoGenerated;
    this->request_backend(generated ? slaposSupportPoints : slaposDrillHoles, use_cache);
}

void SlaSupportPointsGizmo::load_backend_object()
{
    if (!m_active) {
        return;
    }

    const Biz::Slicing::Sla::Object* object = nullptr;
    if (m_slicing_id.has_value()) {
        const SLAObjectOptRef cached{m_project_interactor.sla_object_cache().get_instance(
            {*m_slicing_id, Domain::ObjectID{m_object_id}}
        )};
        if (cached) {
            object = &cached->get();
        }
    }

    std::optional<Transform3d> instance_trafo;
    if (object != nullptr && object->mesh) {
        const auto it = std::ranges::find_if(
            object->instance_trafos,
            [this](const auto& id_and_trafo) { return id_and_trafo.first.id == m_instance_id; }
        );
        if (it != object->instance_trafos.end()) {
            instance_trafo = it->second;
        }
    }

    if (!instance_trafo.has_value()) {
        object = nullptr;
    }

    const std::shared_ptr<const Domain::TriangleMesh> mesh =
        object != nullptr ? object->mesh : nullptr;
    if (mesh != m_backend_mesh) {
        m_backend_mesh = mesh;
        m_backend_trimesh.reset();
        m_backend_geometry.reset();
        if (mesh != nullptr) {
            m_backend_trimesh  = std::make_unique<Scene::TriangleMesh>(mesh);
            m_backend_geometry = Render::geometry_from_triangle_mesh(m_device, m_backend_trimesh->triangles());
        }
    }

    if (object != nullptr) {
        m_object_trafo   = object->object_trafo;
        m_instance_trafo = *instance_trafo;
    }

    const double elevation = object != nullptr ? m_instance_trafo.translation().z() : 0.;
    if (elevation != m_elevation) {
        m_elevation = elevation;
        this->activate_clipper();
    }

    if (!m_dirty) {
        const ModelObject* mo = this->model_object();
        m_points.clear();
        if (mo != nullptr && mo->sla_points_status == PointsStatus::UserModified) {
            for (const SupportPoint& point : mo->sla_support_points) {
                m_points.push_back(Point{point, false});
            }
        } else if (object != nullptr && object->support_points) {
            const Transform3d to_model = m_object_trafo.inverse();
            for (SupportPoint point : *object->support_points) {
                point.pos = (to_model * point.pos.cast<double>()).cast<float>();
                m_points.push_back(Point{point, false});
            }
        }
        m_hovered.reset();
    }

    this->rebuild_mesh_nodes();
    this->rebuild_point_nodes();
    this->update_dialog();
}

Transform3d SlaSupportPointsGizmo::points_to_world() const
{
    if (m_backend_trimesh != nullptr) {
        return m_bed_trafo * m_instance_trafo * m_object_trafo;
    }

    const ModelInstance* instance = this->model_instance();
    return instance != nullptr ? instance->get_matrix() : Transform3d::Identity();
}

Vec3d SlaSupportPointsGizmo::point_world_normal(const size_t index) const
{
    if (m_backend_trimesh == nullptr) {
        return Vec3d::Zero();
    }

    const Vec3f print_position =
        (m_object_trafo * m_points[index].support_point.pos.cast<double>()).cast<float>();
    const indexed_triangle_set& its = m_backend_trimesh->triangles();
    const int facet_idx = get_closest_facet(m_backend_trimesh->aabb_mesh(), print_position);
    if (facet_idx < 0 || size_t(facet_idx) >= its.indices.size()) {
        return Vec3d::Zero();
    }

    const Vec3d normal = TriMesh::its_face_normal(its, its.indices[facet_idx]).cast<double>();
    return ((m_bed_trafo * m_instance_trafo).linear() * normal).normalized();
}

bool SlaSupportPointsGizmo::is_clipped(const Vec3d& world_position) const
{
    return m_clipper.get_position() != 0.
        && m_clipper.get_clipping_plane().is_point_clipped(world_position);
}

std::optional<SlaSupportPointsGizmo::MeshHit> SlaSupportPointsGizmo::raycast(const Scene::Ray& ray
) const
{
    if (m_backend_trimesh == nullptr) {
        return std::nullopt;
    }

    const Transform3d mesh_to_world = m_bed_trafo * m_instance_trafo;
    const std::optional<Biz::Utils::MeshRaycaster::UnprojectResult> result = unproject_on_mesh(
        m_backend_trimesh->aabb_mesh(),
        ray,
        mesh_to_world,
        m_clipper.get_clipping_plane(),
        false
    );
    if (!result.has_value() || this->is_clipped(mesh_to_world * result->position)) {
        return std::nullopt;
    }

    return MeshHit{
        m_object_trafo.inverse() * result->position,
        (m_object_trafo.linear().transpose() * result->normal).normalized(),
        (ray.origin - mesh_to_world * result->position).norm()
    };
}

std::optional<size_t> SlaSupportPointsGizmo::hovered_point(const Scene::GizmoEventContext& ctx
) const
{
    if (ctx.pick_results().empty()) {
        return std::nullopt;
    }

    // The results are sorted by distance, a point counts only if no gizmo mesh is in front of it.
    const Scene::NodePickResult* result = ctx.pick_result_with_tag_of_type<SlaSupportPointNodeTag>();
    if (result == nullptr) {
        return std::nullopt;
    }

    const std::optional<MeshHit> mesh_hit = this->raycast(ctx.pick_ray());
    if (mesh_hit.has_value() && mesh_hit->distance + VisibilityOffset < result->cast.distance) {
        return std::nullopt;
    }

    const size_t index = result->node->tag_of_type<SlaSupportPointNodeTag>()->index;
    return index < m_points.size() ? std::optional{index} : std::nullopt;
}

bool SlaSupportPointsGizmo::is_visible(const Vec3d& world_position) const
{
    const Scene::Camera& camera = m_scene_presenter.scene().camera();
    const Vec3d ndc             = camera.project_to_ndc(world_position);
    const Render::Rect& viewport = camera.viewport();
    const Scene::Ray ray = camera.ray_at(
        viewport.x + 0.5 * (ndc.x() + 1.) * viewport.width,
        viewport.y + 0.5 * (1. - ndc.y()) * viewport.height
    );

    const std::optional<MeshHit> hit = this->raycast(ray);
    return !hit.has_value() || hit->distance >= (ray.origin - world_position).norm();
}

void SlaSupportPointsGizmo::rebuild_mesh_nodes()
{
    if (m_mesh_node == nullptr) {
        return;
    }

    m_scene->remove_children([](const Scene::Node*) { return true; }, m_mesh_node);
    m_model_geometries.clear();

    m_mesh_material =
        Render::Material{}
            .set_shader(m_device.context().shader_manager().shader("gouraud_light_clip"))
            .set_uniform(
                "uniform_color",
                m_backend_geometry != nullptr ? BackendMeshColor : DisabledMeshColor
            );

    const auto add_mesh_node = [this](const Render::Geometry* geometry, const Transform3d& trafo)
    {
        // No shadows: they put the node into the deferred passes, which have no variant of the
        // clipping shader. The forward pass renders it as the paint-on gizmos render theirs.
        Scene::NodeBuilder builder{*m_scene};
        builder.set_debug_name("SlaSupportPointsGizmo - mesh")
            .set_mesh(geometry, m_mesh_material, 0)
            .set_transform(trafo);
        m_scene->add_child(builder.build().release(), m_mesh_node);
    };

    if (m_backend_geometry != nullptr) {
        add_mesh_node(m_backend_geometry.get(), m_bed_trafo * m_instance_trafo);
    } else if (const ModelObject* mo = this->model_object(); mo != nullptr) {
        const ModelInstance* instance = this->model_instance();
        for (const ModelVolume* volume : mo->volumes) {
            if (!volume->is_model_part()) {
                continue;
            }
            m_model_geometries.push_back(Render::geometry_from_triangle_mesh(m_device, volume->mesh()));
            add_mesh_node(
                m_model_geometries.back().get(),
                instance->get_matrix() * volume->get_matrix()
            );
        }
    }

    this->update_clipping();
}

void SlaSupportPointsGizmo::rebuild_point_nodes()
{
    if (m_points_node == nullptr) {
        return;
    }

    m_scene->remove_children([](const Scene::Node*) { return true; }, m_points_node);
    m_point_nodes.clear();

    for (size_t index = 0; index < m_points.size(); ++index) {
        std::array<Scene::Node*, 2> nodes{};
        const std::array<std::pair<const Render::Geometry*, const Scene::TriangleMesh*>, 2> meshes{
            std::pair{m_sphere_geometry.get(), m_sphere_trimesh.get()},
            std::pair{m_cone_geometry.get(), m_cone_trimesh.get()}
        };
        for (size_t mesh_idx = 0; mesh_idx < meshes.size(); ++mesh_idx) {
            Scene::NodeBuilder builder{*m_scene};
            builder.set_debug_name("SlaSupportPointsGizmo - point")
                .set_tag(SlaSupportPointNodeTag{index})
                .set_mesh(
                    meshes[mesh_idx].first,
                    Render::Material{}.set_shader(
                        m_device.context().shader_manager().shader("gouraud_light")
                    ),
                    0
                )
                .set_aabb(meshes[mesh_idx].second->aabb_mesh());
            m_scene->add_child(builder.build().release(), m_points_node);
            nodes[mesh_idx] = m_points_node->children().back().get();
        }
        m_point_nodes.emplace_back(nodes[0], nodes[1]);
        this->update_point_node(index);
    }

    this->update_point_colors();
}

void SlaSupportPointsGizmo::update_point_node(const size_t index)
{
    if (index >= m_point_nodes.size()) {
        return;
    }

    const SupportPoint& point = m_points[index].support_point;
    const Vec3d position      = this->points_to_world() * point.pos.cast<double>();
    const double radius       = point.head_front_radius;
    const auto [sphere_node, cone_node] = m_point_nodes[index];

    Transform3d sphere_trafo = Transform3d::Identity();
    sphere_trafo.translate(position);
    sphere_trafo.scale(radius);
    sphere_node->set_local_transform(sphere_trafo);

    const Vec3d normal = this->point_world_normal(index);
    if (normal.isZero()) {
        cone_node->set_enabled(false);
    } else {
        Transform3d cone_trafo = Transform3d::Identity();
        cone_trafo.translate(position);
        cone_trafo.rotate(Eigen::Quaterniond::FromTwoVectors(Vec3d::UnitZ(), normal));
        cone_trafo.translate((ConeHeight + radius) * Vec3d::UnitZ());
        cone_trafo.rotate(Eigen::AngleAxisd(std::numbers::pi, Vec3d::UnitX()));
        cone_trafo.scale(Vec3d{ConeRadius, ConeRadius, ConeHeight});
        cone_node->set_local_transform(cone_trafo);
        cone_node->set_enabled(!this->is_clipped(position));
    }

    sphere_node->set_enabled(!this->is_clipped(position));
}

void SlaSupportPointsGizmo::update_point_colors()
{
    for (size_t index = 0; index < m_point_nodes.size(); ++index) {
        const Point& point = m_points[index];
        ColorRGBA color    = point.support_point.type == SupportPointType::manual_add ?
               ManualPointColor :
            point.support_point.type == SupportPointType::island ? IslandPointColor :
                                                                   GeneratedPointColor;
        if (m_hovered == index) {
            color = HoveredPointColor;
        } else if (point.selected) {
            color = SelectedPointColor;
        }

        const Render::Material material =
            Render::Material{}
                .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                .set_uniform("uniform_color", color)
                .set_uniform("emission_factor", 0.5f);
        m_point_nodes[index].first->set_material_override(material);
        m_point_nodes[index].second->set_material_override(material);
    }
}

void SlaSupportPointsGizmo::update_clipping()
{
    if (m_mesh_node == nullptr) {
        return;
    }

    Domain::Vec4f clipping_plane{0.f, 0.f, 1.f, FLT_MAX};
    if (m_clipper.get_position() != 0.) {
        // The normal is stored with the opposite sign than the shader needs.
        const Biz::ClippingPlane& plane = m_clipper.get_clipping_plane();
        for (size_t i = 0; i < 3; ++i) {
            clipping_plane[i] = -float(plane.get_data()[i]);
        }
        clipping_plane[3] = float(plane.get_data()[3]);
    }

    Render::Material material = m_mesh_material;
    material.set_uniform("clipping_plane", clipping_plane);
    for (const auto& node : m_mesh_node->children()) {
        node->set_material_override(material);
    }

    for (size_t index = 0; index < m_point_nodes.size(); ++index) {
        this->update_point_node(index);
    }
}

void SlaSupportPointsGizmo::update_dialog()
{
    if (!m_active) {
        return;
    }

    std::string status;
    if (!m_slicing_id.has_value()) {
        status = _u8L("The object has to be printable and placed on a bed.");
    } else if (m_requested_step == slaposSupportPoints) {
        status = _u8L("Generating support points...");
    } else if (!this->input_enabled()) {
        status = m_requested_step.has_value() ?
            _u8L("Preparing the object...") :
            _u8L("The object couldn't be prepared, check the print settings.");
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
    if (m_dirty) {
        status += "\n" + _u8L("The changes are not applied yet.");
    }
    m_dialog_ptr->set_status(status);

    m_dialog_ptr->set_input_enabled(this->input_enabled());
    m_dialog_ptr->set_apply_enabled(m_dirty);
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
            if (item.name == "support_points_density_relative" && item.config_item != nullptr) {
                m_dialog_ptr->set_density(item.config_item->get<int>());
            }
        }
    }
}

void SlaSupportPointsGizmo::edited(const UndoSnapshotType snapshot_type)
{
    m_dirty = true;
    this->update_dialog();
    m_project_interactor.undo_provider().take_snapshot(snapshot_type);
}

void SlaSupportPointsGizmo::add_point(const MeshHit& hit)
{
    m_points.push_back(Point{
        SupportPoint{
            hit.position.cast<float>(),
            float(m_head_diameter / 2.),
            SupportPointType::manual_add
        },
        false
    });
    this->rebuild_point_nodes();
    this->edited(UndoSnapshotType::SlaSupportPointsAdd);
}

void SlaSupportPointsGizmo::select_point(const size_t index, const bool exclusive)
{
    if (exclusive) {
        for (Point& point : m_points) {
            point.selected = false;
        }
    }
    m_points[index].selected = true;
    m_head_diameter          = 2. * m_points[index].support_point.head_front_radius;
    this->update_point_colors();
    this->update_dialog();
}

void SlaSupportPointsGizmo::unselect_point(const size_t index)
{
    m_points[index].selected = false;
    this->update_point_colors();
}

void SlaSupportPointsGizmo::select_all(const bool select)
{
    for (Point& point : m_points) {
        point.selected = select;
    }
    this->update_point_colors();
}

void SlaSupportPointsGizmo::delete_selected(const bool force)
{
    const size_t erased = std::erase_if(
        m_points,
        [this, force](const Point& point)
        { return point.selected && (force || !m_lock_islands || !point.support_point.is_island()); }
    );
    if (erased == 0) {
        return;
    }

    m_hovered.reset();
    this->rebuild_point_nodes();
    this->edited(UndoSnapshotType::SlaSupportPointsDelete);
}

void SlaSupportPointsGizmo::select_by_rectangle(
    const Rectangle& rectangle,
    const Render::ScreenInfo&
)
{
    const Scene::Camera& camera  = m_scene_presenter.scene().camera();
    const Render::Rect& viewport = camera.viewport();
    const Vec2d min              = rectangle.start.cwiseMin(rectangle.end);
    const Vec2d max              = rectangle.start.cwiseMax(rectangle.end);
    const Transform3d to_world   = this->points_to_world();

    for (size_t index = 0; index < m_points.size(); ++index) {
        const Vec3d position = to_world * m_points[index].support_point.pos.cast<double>();
        if (this->is_clipped(position)) {
            continue;
        }

        const Vec3d ndc = camera.project_to_ndc(position);
        const Vec2d screen{
            viewport.x + 0.5 * (ndc.x() + 1.) * viewport.width,
            viewport.y + 0.5 * (1. - ndc.y()) * viewport.height
        };
        if ((screen.array() < min.array()).any() || (screen.array() > max.array()).any()) {
            continue;
        }

        // Check slightly above the surface, so the point doesn't hide under every irregularity.
        if (!this->is_visible(position + VisibilityOffset * this->point_world_normal(index))) {
            continue;
        }

        m_points[index].selected = rectangle.select;
    }

    this->update_point_colors();
}

void SlaSupportPointsGizmo::set_head_diameter(const double head_diameter)
{
    m_head_diameter = head_diameter;

    bool changed = false;
    for (size_t index = 0; index < m_points.size(); ++index) {
        if (m_points[index].selected) {
            m_points[index].support_point.head_front_radius = float(head_diameter / 2.);
            this->update_point_node(index);
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
        if (item.name == "support_points_density_relative" && item.config_item != nullptr) {
            m_project_interactor.preset_interactor()
                .set_item_value(*item.config_item, Domain::ConfigValue{density}, {0});
            // As in the old gizmo: a new density means new points.
            this->auto_generate();
            return;
        }
    }
}

void SlaSupportPointsGizmo::remove_all()
{
    if (m_points.empty()) {
        return;
    }

    m_points.clear();
    m_hovered.reset();
    this->rebuild_point_nodes();
    this->edited(UndoSnapshotType::SlaSupportPointsRemoveAll);
}

void SlaSupportPointsGizmo::auto_generate()
{
    const ModelObject* mo = this->model_object();
    if (mo == nullptr) {
        return;
    }

    const bool user_points =
        mo->sla_points_status == PointsStatus::UserModified || !mo->sla_support_points.empty();
    const bool was_dirty = m_dirty;
    m_dirty              = false;

    if (user_points) {
        m_scene_interactor.modify_sla_support_points(
            Domain::ElementRef{m_object_id},
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

void SlaSupportPointsGizmo::apply()
{
    this->flush();
    this->request_backend(slaposSupportPoints, false);
}

void SlaSupportPointsGizmo::flush()
{
    if (!m_dirty) {
        return;
    }

    m_dirty = false;
    if (this->model_object() == nullptr) {
        return;
    }

    Domain::SLA::SupportPoints points;
    points.reserve(m_points.size());
    for (const Point& point : m_points) {
        points.push_back(point.support_point);
    }

    m_scene_interactor.modify_sla_support_points(
        Domain::ElementRef{m_object_id},
        [&points](ModelObject& model_object)
        {
            model_object.sla_support_points = std::move(points);
            model_object.sla_points_status  = PointsStatus::UserModified;
        }
    );
    m_project_interactor.undo_provider().take_snapshot(UndoSnapshotType::SlaSupportPointsApply);
    this->update_dialog();
}

void SlaSupportPointsGizmo::register_commands(Platform::CommandRegistry& registry)
{
    using Platform::FuncCommand;
    using Platform::FuncCommandExtraOpts;
    using Platform::KeyboardShortcut;
    using Platform::KeyboardShortcuts;
    using Platform::KeyCode;
    using Platform::KeyModifier;
    using Platform::KeyModifiers;

    const auto enabled = [this]() { return this->input_enabled(); };

    registry
        .register_command(
            std::make_unique<FuncCommand>(
                "sla-support-points-gizmo-delete-selected",
                [this]() { this->delete_selected(false); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        KeyboardShortcuts{
                            KeyboardShortcut{0, KeyCode::Delete},
                            KeyboardShortcut{0, KeyCode::Backspace}
                        },
                    .enabled = enabled
                }
            )
        )
        .register_command(
            std::make_unique<FuncCommand>(
                "sla-support-points-gizmo-select-all",
                [this]() { this->select_all(true); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        KeyboardShortcuts{
                            KeyboardShortcut{KeyModifiers(KeyModifier::Ctrl), KeyCode::A}
                        },
                    .enabled = enabled
                }
            )
        )
        .register_command(
            std::make_unique<FuncCommand>(
                "sla-support-points-gizmo-apply",
                [this]() { this->apply(); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = KeyboardShortcuts{KeyboardShortcut{0, KeyCode::Return}},
                    .enabled            = [this]() { return this->input_enabled() && m_dirty; }
                }
            )
        );
}

Scene::GizmoActivationState
SlaSupportPointsGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    using Platform::KeyModifier;
    using Platform::KeyModifiers;
    using Platform::MouseButton;
    using Platform::MouseEvent;
    using Scene::GizmoActivationState;

    if (!this->input_enabled()) {
        return GizmoActivationState::Inactive;
    }

    const MouseEvent& event = ctx.mouse_event();
    const Vec2d mouse{ctx.screen_mouse_x(), ctx.screen_mouse_y()};
    const bool left  = (event.button() & MouseButton::Left) == MouseButton::Left;
    const bool right = (event.button() & MouseButton::Right) == MouseButton::Right;
    const bool shift = (event.key_modifiers() & KeyModifiers(KeyModifier::Shift)) != 0;
    const bool alt   = (event.key_modifiers() & KeyModifiers(KeyModifier::Alt)) != 0;
    const bool ctrl  = (event.key_modifiers() & KeyModifiers(KeyModifier::Ctrl)) != 0;

    switch (event.type()) {
    case MouseEvent::Type::Move:
        if (m_dragged.has_value()) {
            Point& point = m_points[*m_dragged];
            if (!(m_lock_islands && point.support_point.is_island())) {
                if (const std::optional<MeshHit> hit = this->raycast(ctx.pick_ray())) {
                    point.support_point.pos  = hit->position.cast<float>();
                    point.support_point.type = SupportPointType::manual_add;
                    this->update_point_node(*m_dragged);
                    this->update_point_colors();
                }
            }
            return GizmoActivationState::Active;
        }
        if (m_rectangle.has_value()) {
            m_rectangle->end = mouse;
            return GizmoActivationState::Active;
        }
        if (m_wait_for_up) {
            return GizmoActivationState::Active;
        }
        if (const std::optional<size_t> hovered = this->hovered_point(ctx); hovered != m_hovered) {
            m_hovered = hovered;
            this->update_point_colors();
        }
        return GizmoActivationState::Inactive;

    case MouseEvent::Type::ButtonDown:
        if (ctrl) {
            return GizmoActivationState::Inactive;
        }
        m_hovered = this->hovered_point(ctx);
        if (left) {
            if (shift || alt) {
                if (m_hovered.has_value()) {
                    shift ? this->select_point(*m_hovered, false) : this->unselect_point(*m_hovered);
                    m_wait_for_up = true;
                } else {
                    m_rectangle = Rectangle{mouse, mouse, shift};
                }
                return GizmoActivationState::Active;
            }
            if (m_hovered.has_value()) {
                this->select_point(*m_hovered, true);
                m_dragged           = m_hovered;
                m_point_before_drag = m_points[*m_hovered];
                return GizmoActivationState::Active;
            }
            if (std::ranges::any_of(m_points, [](const Point& point) { return point.selected; })) {
                // A click next to the selected points only deselects them.
                this->select_all(false);
                m_wait_for_up = true;
                return GizmoActivationState::Active;
            }
            if (const std::optional<MeshHit> hit = this->raycast(ctx.pick_ray())) {
                this->add_point(*hit);
                m_wait_for_up = true;
                return GizmoActivationState::Active;
            }
            return GizmoActivationState::Inactive;
        }
        if (right && m_hovered.has_value()) {
            this->select_point(*m_hovered, true);
            this->delete_selected(false);
            m_wait_for_up = true;
            return GizmoActivationState::Active;
        }
        return GizmoActivationState::Inactive;

    case MouseEvent::Type::ButtonUp:
        if (m_dragged.has_value()) {
            const bool moved = m_point_before_drag.has_value()
                && m_points[*m_dragged].support_point.pos != m_point_before_drag->support_point.pos;
            m_dragged.reset();
            m_point_before_drag.reset();
            if (moved) {
                this->edited(UndoSnapshotType::SlaSupportPointsMove);
            }
            return GizmoActivationState::Done;
        }
        if (m_rectangle.has_value()) {
            this->select_by_rectangle(*m_rectangle, ctx.screen_info());
            m_rectangle.reset();
            return GizmoActivationState::Done;
        }
        if (m_wait_for_up) {
            m_wait_for_up = false;
            return GizmoActivationState::Done;
        }
        return GizmoActivationState::Inactive;

    default:
        return GizmoActivationState::Inactive;
    }
}

void SlaSupportPointsGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    this->render_rectangle(cmd_buffer);
}

void SlaSupportPointsGizmo::render_rectangle(Render::CommandBuffer& cmd_buffer)
{
    if (!m_rectangle.has_value() || m_rectangle->start == m_rectangle->end) {
        return;
    }

    if (m_rectangle_geometry == nullptr) {
        m_rectangle_material =
            Render::Material{}.set_shader(m_device.context().shader_manager().shader("flat"));
        m_rectangle_geometry = std::make_unique<Render::Geometry>(m_device);
        Render::GeometryBuilder<Render::VertexP3> builder;
        builder.add_vertex({{0.f, 0.f, 0.f}})
            .add_vertex({{1.f, 0.f, 0.f}})
            .add_vertex({{1.f, 1.f, 0.f}})
            .add_vertex({{0.f, 1.f, 0.f}})
            .add_draw_command({Render::PrimitiveType::LineLoop, 0, 4, m_rectangle_material});
        builder.update(*m_rectangle_geometry);
    }

    const Render::Rect& viewport = m_scene_presenter.scene().camera().viewport();
    const Vec2d min              = m_rectangle->start.cwiseMin(m_rectangle->end);
    const Vec2d max              = m_rectangle->start.cwiseMax(m_rectangle->end);
    const auto to_ndc_x = [&viewport](double x) { return 2. * (x - viewport.x) / viewport.width - 1.; };
    const auto to_ndc_y = [&viewport](double y) { return 1. - 2. * (y - viewport.y) / viewport.height; };

    Domain::SquareMatrix4f matrix = Domain::SquareMatrix4f::Identity();
    matrix(0, 0) = float(to_ndc_x(max.x()) - to_ndc_x(min.x()));
    matrix(1, 1) = float(to_ndc_y(min.y()) - to_ndc_y(max.y()));
    matrix(0, 3) = float(to_ndc_x(min.x()));
    matrix(1, 3) = float(to_ndc_y(max.y()));

    m_rectangle_material.set_uniform(
        "uniform_color",
        m_rectangle->select ? ColorRGBA{0.3f, 1.f, 0.3f, 1.f} : ColorRGBA{1.f, 0.3f, 0.3f, 1.f}
    );
    m_rectangle_material.set_uniform("projection_view_model_matrix", matrix);
    cmd_buffer.bind_and_draw(*m_rectangle_geometry, m_rectangle_material);
}

} // namespace Slic3r::App::Plater
