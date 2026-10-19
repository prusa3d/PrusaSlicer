#include "Slic3r/App/Plater/SlaGizmoBase.hpp"

#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Render/GeometryBuilder.hpp"
#include "Slic3r/App/Scene/NodeBuilder.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"
#include "Slic3r/App/Scene/TriangleMeshManager.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/SLAObjectRequest.hpp"
#include "Slic3r/Biz/Utils/MeshRaycaster.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/ModelVolume.hpp"

#include "libslic3r/SLAResult.hpp"

#include <magic_enum/magic_enum_flags.hpp>

#include <algorithm>

using namespace magic_enum::bitwise_operators;

using Slic3r::App::Scene::SceneNodeTag;
using Slic3r::Biz::_u8L;
using Slic3r::Biz::SLAObjectOptRef;
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

namespace TriMesh = Slic3r::Biz::Algorithms::TriangleMesh;

namespace Slic3r::App::Plater {

// An item is visible from the camera if this point above the surface along its direction is.
static constexpr double VisibilityOffset = 0.2;

static const ColorRGBA BackendMeshColor{0.85f, 0.85f, 0.85f, 1.f};
static const ColorRGBA DisabledMeshColor{0.35f, 0.35f, 0.35f, 1.f};
static const ColorRGBA SelectedItemColor{1.f, 0.f, 0.25f, 1.f};
static const ColorRGBA HoveredItemColor{0.f, 1.f, 1.f, 1.f};

SlaGizmoBase::SlaGizmoBase(
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

    m_project_interactor.sla_object_cache().add_listener<Biz::ISLAObjectCacheChangedListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

SlaGizmoBase::~SlaGizmoBase()
{
    m_project_interactor.sla_object_cache().remove_listener<Biz::ISLAObjectCacheChangedListener>(
        this
    );
    m_scene_interactor.remove_listener<Biz::Scene::ISceneChangedListener>(this);
    m_scene_interactor.remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

bool SlaGizmoBase::supports_printer(const PrinterTechnology pt) const
{
    return pt == PrinterTechnology::SLA;
}

bool SlaGizmoBase::enabled() const
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

bool SlaGizmoBase::disable_object_selection() const
{
    return true;
}

void SlaGizmoBase::provide_gizmo_controller(Scene::IGizmoController& controller)
{
    m_gizmo_controller = &controller;
}

void SlaGizmoBase::restore_items(const bool dirty)
{
    m_hovered.reset();
    m_dirty = dirty;
    if (m_dirty) {
        this->rebuild_item_nodes();
        this->update_dialog();
    } else {
        this->load_backend_object();
        // The restored model reaches the object cache later, don't take the old items from it.
        this->request_backend(this->step_to_show(), false);
    }
}

void SlaGizmoBase::on_activated()
{
    if (!this->find_target()) {
        m_gizmo_controller->deactivate_current_tool();
        return;
    }

    m_active = true;
    this->init_scene();
    this->on_target_activated();
    this->load_backend_object();

    // Items the user didn't apply before switching to another project.
    const auto saved{m_saved_caches.find(m_project_interactor.selected_project_id())};
    if (saved != m_saved_caches.end()) {
        if (saved->second.object_id == m_object_id && saved->second.instance_id == m_instance_id) {
            this->set_tool_state(saved->second.state);
        }
        m_saved_caches.erase(saved);
    }

    this->request_backend(this->step_to_show());
    this->update_dialog();
}

void SlaGizmoBase::on_deactivated()
{
    if (!m_active) {
        return;
    }

    // Undo/redo restores the model and the items together, nothing to write.
    if (!m_project_interactor.undo_provider().is_restoring_snapshot()) {
        this->flush();
    }

    this->release_scene();
    this->reset_target();
}

void SlaGizmoBase::on_project_activated(size_t)
{
    this->on_activated();
}

void SlaGizmoBase::on_project_deactivated(const size_t old_project_id)
{
    if (!m_active) {
        return;
    }

    // The selected project has already changed, so the items can't be written into the model.
    // Keep them until the project is selected again.
    if (m_dirty) {
        if (const std::optional<Undo::ToolState> state = this->get_tool_state()) {
            m_saved_caches[old_project_id] = SavedCache{m_object_id, m_instance_id, *state};
        }
    }

    this->release_scene();
    this->reset_target();
}

bool SlaGizmoBase::find_target()
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

void SlaGizmoBase::reset_target()
{
    m_request->cancel();
    m_requested_step.reset();

    m_active = false;
    m_slicing_id.reset();
    this->clear_items();
    m_dirty = false;

    m_backend_mesh.reset();
    m_backend_trimesh.reset();
    m_backend_geometry.reset();
    m_model_geometries.clear();

    m_hovered.reset();
    m_dragged.reset();
    m_wait_for_up = false;
    m_rectangle.reset();
}

const ModelObject* SlaGizmoBase::model_object() const
{
    return m_project_interactor.selected_project().find_object_by_id(m_object_id);
}

const ModelInstance* SlaGizmoBase::model_instance() const
{
    return m_project_interactor.selected_project().find_instance_by_id(m_object_id, m_instance_id);
}

bool SlaGizmoBase::input_enabled() const
{
    return m_active && m_backend_trimesh != nullptr;
}

std::string SlaGizmoBase::common_status() const
{
    if (!m_slicing_id.has_value()) {
        return _u8L("The object has to be printable and placed on a bed.");
    }
    if (!this->input_enabled()) {
        return m_requested_step.has_value() ?
            _u8L("Preparing the object...") :
            _u8L("The object couldn't be prepared, check the print settings.");
    }
    return {};
}

void SlaGizmoBase::init_scene()
{
    m_scene = &m_scene_presenter.scene();

    Scene::NodeBuilder builder{*m_scene};
    builder.set_debug_name("SlaGizmoBase - main node");
    m_scene->add_child(builder.build().release(), &m_scene->root());
    m_main_node = m_scene->root().children().back().get();

    for (Scene::Node** node : {&m_mesh_node, &m_items_node, &m_clipper_node}) {
        Scene::NodeBuilder child_builder{*m_scene};
        child_builder.set_debug_name("SlaGizmoBase - child node");
        m_scene->add_child(child_builder.build().release(), m_main_node);
        *node = m_main_node->children().back().get();
    }

    this->hide_plater_nodes();
    m_scene->add_listener<Scene::ISceneChangedListener>(this);
    m_scene->add_listener<Scene::IThumbnailRenderListener>(this);

    m_elevation = 0.;
    this->activate_clipper();
    m_clipper_presenter.set_position_by_ratio(0., false);
    this->reset_dialog_clipping();
}

void SlaGizmoBase::activate_clipper()
{
    // The clipper cuts the backend mesh as the gizmo shows it (the model before it is there).
    const double position = m_clipper.get_position();
    m_clipper_presenter.activate(
        this->model_object(),
        this->model_instance(),
        m_clipper_node,
        m_elevation,
        Scene::BuildMeshesNodes::No,
        m_backend_trimesh != nullptr ? m_backend_mesh : nullptr,
        Domain::Transformation{m_object_trafo.inverse()}
    );
    m_clipper_presenter.set_behavior(true, true, 0.);
    m_clipper_presenter.set_position_by_ratio(position, true);
}

void SlaGizmoBase::release_scene()
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
    m_items_node   = nullptr;
    m_clipper_node = nullptr;
    m_item_nodes.clear();
    m_scene = nullptr;
}

void SlaGizmoBase::hide_plater_nodes()
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

void SlaGizmoBase::restore_plater_nodes()
{
    for (Scene::Node* node : m_hidden_nodes) {
        node->set_enabled(true);
    }
    m_hidden_nodes.clear();
}

void SlaGizmoBase::on_node_added(Scene::Node* node)
{
    // E.g. undo recreates the nodes of the model.
    if (node->tag_of_type<SceneNodeTag>() != nullptr) {
        m_hidden_nodes.push_back(node);
        node->set_enabled(false);
    }
}

void SlaGizmoBase::on_node_removed(Scene::Node* node)
{
    std::erase(m_hidden_nodes, node);
}

void SlaGizmoBase::on_thumbnail_render_begin()
{
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(false);
    }
    for (Scene::Node* node : m_hidden_nodes) {
        node->set_enabled(true);
    }
}

void SlaGizmoBase::on_thumbnail_render_end()
{
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(true);
    }
    for (Scene::Node* node : m_hidden_nodes) {
        node->set_enabled(false);
    }
}

void SlaGizmoBase::on_model_reloaded(const Domain::SelectionId project_id)
{
    if (!m_active || project_id != m_project_interactor.selected_project_id()) {
        return;
    }

    // E.g. undo/redo replaced the model. The items come from the restored tool state, if any.
    const ModelInstance* instance = this->model_instance();
    if (instance == nullptr) {
        // The object is gone, there is nothing to write the items to.
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

void SlaGizmoBase::on_scene_selection_changed(
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

void SlaGizmoBase::on_sla_object_cache_changed(
    const Domain::SlicingId& id,
    const Domain::ObjectID object_id
)
{
    if (m_active && m_slicing_id == id && object_id.id == m_object_id) {
        this->load_backend_object();
    }
}

void SlaGizmoBase::request_backend(const SLAPrintObjectStep step, const bool use_cache)
{
    if (!m_slicing_id.has_value()) {
        return;
    }

    m_requested_step = step;
    m_request->start(*m_slicing_id, Domain::ObjectID{m_object_id}, step, use_cache);
    this->update_dialog();
}

void SlaGizmoBase::load_backend_object()
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
    const bool mesh_changed = mesh != m_backend_mesh;
    if (mesh_changed) {
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
    if (mesh_changed || elevation != m_elevation) {
        m_elevation = elevation;
        this->activate_clipper();
    }

    if (!m_dirty) {
        this->load_items(object);
        m_hovered.reset();
    }

    this->rebuild_mesh_nodes();
    this->rebuild_item_nodes();
    this->update_dialog();
}

Transform3d SlaGizmoBase::items_to_world() const
{
    if (m_backend_trimesh != nullptr) {
        return m_bed_trafo * m_instance_trafo * m_object_trafo;
    }

    const ModelInstance* instance = this->model_instance();
    return instance != nullptr ? instance->get_matrix() : Transform3d::Identity();
}

Vec3d SlaGizmoBase::surface_world_normal(const Vec3d& model_position) const
{
    if (m_backend_trimesh == nullptr) {
        return Vec3d::Zero();
    }

    const Vec3f print_position = (m_object_trafo * model_position).cast<float>();
    const indexed_triangle_set& its = m_backend_trimesh->triangles();
    const int facet_idx = get_closest_facet(m_backend_trimesh->aabb_mesh(), print_position);
    if (facet_idx < 0 || size_t(facet_idx) >= its.indices.size()) {
        return Vec3d::Zero();
    }

    const Vec3d normal = TriMesh::its_face_normal(its, its.indices[facet_idx]).cast<double>();
    return ((m_bed_trafo * m_instance_trafo).linear() * normal).normalized();
}

Vec3d SlaGizmoBase::model_direction_to_world(const Vec3d& direction) const
{
    return (this->items_to_world().linear() * direction).normalized();
}

Vec3d SlaGizmoBase::print_direction_to_model(const Vec3d& direction) const
{
    return (m_object_trafo.linear().inverse() * direction).normalized();
}

bool SlaGizmoBase::is_clipped(const Vec3d& world_position) const
{
    return m_clipper.get_position() != 0.
        && m_clipper.get_clipping_plane().is_point_clipped(world_position);
}

std::optional<SlaGizmoBase::MeshHit> SlaGizmoBase::raycast(const Scene::Ray& ray) const
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
        result->normal,
        (ray.origin - mesh_to_world * result->position).norm()
    };
}

std::optional<size_t> SlaGizmoBase::hovered_item(const Scene::GizmoEventContext& ctx) const
{
    if (ctx.pick_results().empty()) {
        return std::nullopt;
    }

    // The results are sorted by distance, an item counts only if no gizmo mesh is in front of it.
    const Scene::NodePickResult* result = ctx.pick_result_with_tag_of_type<SlaGizmoItemNodeTag>();
    if (result == nullptr) {
        return std::nullopt;
    }

    const std::optional<MeshHit> mesh_hit = this->raycast(ctx.pick_ray());
    if (mesh_hit.has_value() && mesh_hit->distance + VisibilityOffset < result->cast.distance) {
        return std::nullopt;
    }

    const size_t index = result->node->tag_of_type<SlaGizmoItemNodeTag>()->index;
    return index < this->item_count() ? std::optional{index} : std::nullopt;
}

bool SlaGizmoBase::is_visible(const Vec3d& world_position) const
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

void SlaGizmoBase::rebuild_mesh_nodes()
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
        builder.set_debug_name("SlaGizmoBase - mesh")
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

void SlaGizmoBase::rebuild_item_nodes()
{
    if (m_items_node == nullptr) {
        return;
    }

    m_scene->remove_children([](const Scene::Node*) { return true; }, m_items_node);
    m_item_nodes.clear();

    const std::vector<ItemMesh> meshes = this->item_meshes();
    for (size_t index = 0; index < this->item_count(); ++index) {
        std::vector<Scene::Node*>& nodes = m_item_nodes.emplace_back();
        for (const ItemMesh& mesh : meshes) {
            Scene::NodeBuilder builder{*m_scene};
            builder.set_debug_name("SlaGizmoBase - item")
                .set_tag(SlaGizmoItemNodeTag{index})
                .set_mesh(
                    mesh.geometry,
                    Render::Material{}.set_shader(
                        m_device.context().shader_manager().shader("gouraud_light")
                    ),
                    0
                )
                .set_aabb(mesh.trimesh->aabb_mesh());
            m_scene->add_child(builder.build().release(), m_items_node);
            nodes.push_back(m_items_node->children().back().get());
        }
        this->update_item_node(index);
    }

    this->update_item_colors();
}

void SlaGizmoBase::update_item_node(const size_t index)
{
    if (index >= m_item_nodes.size()) {
        return;
    }

    const bool clipped = this->is_clipped(this->items_to_world() * this->item_position(index));
    const std::vector<std::optional<Transform3d>> trafos = this->item_mesh_trafos(index);
    const std::vector<Scene::Node*>& nodes                = m_item_nodes[index];
    for (size_t mesh_idx = 0; mesh_idx < nodes.size() && mesh_idx < trafos.size(); ++mesh_idx) {
        if (trafos[mesh_idx].has_value()) {
            nodes[mesh_idx]->set_local_transform(*trafos[mesh_idx]);
        }
        nodes[mesh_idx]->set_enabled(trafos[mesh_idx].has_value() && !clipped);
    }
}

void SlaGizmoBase::update_item_colors()
{
    for (size_t index = 0; index < m_item_nodes.size(); ++index) {
        ColorRGBA color = this->item_color(index);
        if (m_hovered == index) {
            color = HoveredItemColor;
        } else if (this->item_selected(index)) {
            color = SelectedItemColor;
        }

        const Render::Material material =
            Render::Material{}
                .set_shader(m_device.context().shader_manager().shader("gouraud_light"))
                .set_uniform("uniform_color", color)
                .set_uniform("emission_factor", 0.5f);
        for (Scene::Node* node : m_item_nodes[index]) {
            node->set_material_override(material);
        }
    }
}

void SlaGizmoBase::update_clipping()
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

    for (size_t index = 0; index < m_item_nodes.size(); ++index) {
        this->update_item_node(index);
    }
}

void SlaGizmoBase::set_clipping_position(const double position)
{
    m_clipper_presenter.set_position_by_ratio(position, true);
    this->update_clipping();
}

void SlaGizmoBase::reset_clipping_direction()
{
    m_clipper_presenter.set_position_by_ratio(-1., false);
    this->update_clipping();
}

void SlaGizmoBase::edited(const Edit edit)
{
    this->edited(this->snapshot_type(edit));
}

void SlaGizmoBase::edited(const Biz::UndoSnapshotType snapshot_type)
{
    m_dirty = true;
    this->update_dialog();
    m_project_interactor.undo_provider().take_snapshot(snapshot_type);
}

void SlaGizmoBase::select_item(const size_t index, const bool exclusive)
{
    if (exclusive) {
        for (size_t i = 0; i < this->item_count(); ++i) {
            this->set_item_selected(i, false);
        }
    }
    this->set_item_selected(index, true);
    this->on_item_selected(index);
    this->update_item_colors();
    this->update_dialog();
}

void SlaGizmoBase::unselect_item(const size_t index)
{
    this->set_item_selected(index, false);
    this->update_item_colors();
}

void SlaGizmoBase::select_all(const bool select)
{
    for (size_t index = 0; index < this->item_count(); ++index) {
        this->set_item_selected(index, select);
    }
    this->update_item_colors();
}

void SlaGizmoBase::delete_selected(const bool force)
{
    if (this->erase_selected_items(force) == 0) {
        return;
    }

    m_hovered.reset();
    this->rebuild_item_nodes();
    this->edited(Edit::Delete);
}

void SlaGizmoBase::remove_all()
{
    if (this->item_count() == 0) {
        return;
    }

    this->clear_items();
    m_hovered.reset();
    this->rebuild_item_nodes();
    this->edited(Edit::RemoveAll);
}

void SlaGizmoBase::select_by_rectangle(const Rectangle& rectangle)
{
    const Scene::Camera& camera  = m_scene_presenter.scene().camera();
    const Render::Rect& viewport = camera.viewport();
    const Vec2d min              = rectangle.start.cwiseMin(rectangle.end);
    const Vec2d max              = rectangle.start.cwiseMax(rectangle.end);
    const Transform3d to_world   = this->items_to_world();

    for (size_t index = 0; index < this->item_count(); ++index) {
        const Vec3d position = to_world * this->item_position(index);
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

        // Check slightly above the surface, so the item doesn't hide under every irregularity.
        if (!this->is_visible(position + VisibilityOffset * this->item_out_direction(index))) {
            continue;
        }

        this->set_item_selected(index, rectangle.select);
    }

    this->update_item_colors();
}

void SlaGizmoBase::apply()
{
    this->flush();
    this->request_backend(this->step_after_apply(), false);
}

void SlaGizmoBase::flush()
{
    if (!m_dirty) {
        return;
    }

    m_dirty = false;
    if (this->model_object() == nullptr) {
        return;
    }

    m_scene_interactor.modify_sla_object_data(
        Domain::ElementRef{m_object_id},
        [this](ModelObject& model_object) { this->write_items(model_object); }
    );
    m_project_interactor.undo_provider().take_snapshot(this->snapshot_type(Edit::Apply));
    this->update_dialog();
}

void SlaGizmoBase::register_commands(Platform::CommandRegistry& registry)
{
    using Platform::FuncCommand;
    using Platform::FuncCommandExtraOpts;
    using Platform::KeyboardShortcut;
    using Platform::KeyboardShortcuts;
    using Platform::KeyCode;
    using Platform::KeyModifier;
    using Platform::KeyModifiers;

    const auto enabled       = [this]() { return this->input_enabled(); };
    const std::string prefix = this->command_prefix();

    registry
        .register_command(
            std::make_unique<FuncCommand>(
                prefix + "-delete-selected",
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
                prefix + "-select-all",
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
                prefix + "-apply",
                [this]() { this->apply(); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = KeyboardShortcuts{KeyboardShortcut{0, KeyCode::Return}},
                    .enabled            = [this]() { return this->input_enabled() && m_dirty; }
                }
            )
        );
}

Scene::GizmoActivationState
SlaGizmoBase::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
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
            if (const std::optional<MeshHit> hit = this->raycast(ctx.pick_ray())) {
                if (this->move_item(*m_dragged, *hit)) {
                    this->update_item_node(*m_dragged);
                    this->update_item_colors();
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
        if (const std::optional<size_t> hovered = this->hovered_item(ctx); hovered != m_hovered) {
            m_hovered = hovered;
            this->update_item_colors();
        }
        return GizmoActivationState::Inactive;

    case MouseEvent::Type::ButtonDown:
        if (ctrl) {
            return GizmoActivationState::Inactive;
        }
        m_hovered = this->hovered_item(ctx);
        if (left) {
            if (shift || alt) {
                if (m_hovered.has_value()) {
                    shift ? this->select_item(*m_hovered, false) : this->unselect_item(*m_hovered);
                    m_wait_for_up = true;
                } else {
                    m_rectangle = Rectangle{mouse, mouse, shift};
                }
                return GizmoActivationState::Active;
            }
            if (m_hovered.has_value()) {
                this->select_item(*m_hovered, true);
                m_dragged = m_hovered;
                this->remember_item(*m_hovered);
                return GizmoActivationState::Active;
            }
            for (size_t index = 0; index < this->item_count(); ++index) {
                if (this->item_selected(index)) {
                    // A click next to the selected items only deselects them.
                    this->select_all(false);
                    m_wait_for_up = true;
                    return GizmoActivationState::Active;
                }
            }
            if (const std::optional<MeshHit> hit = this->raycast(ctx.pick_ray())) {
                this->add_item(*hit);
                this->rebuild_item_nodes();
                this->edited(Edit::Add);
                m_wait_for_up = true;
                return GizmoActivationState::Active;
            }
            return GizmoActivationState::Inactive;
        }
        if (right && m_hovered.has_value()) {
            this->select_item(*m_hovered, true);
            this->delete_selected(false);
            m_wait_for_up = true;
            return GizmoActivationState::Active;
        }
        return GizmoActivationState::Inactive;

    case MouseEvent::Type::ButtonUp:
        if (m_dragged.has_value()) {
            const bool moved = this->item_changed(*m_dragged);
            m_dragged.reset();
            if (moved) {
                this->edited(Edit::Move);
            }
            return GizmoActivationState::Done;
        }
        if (m_rectangle.has_value()) {
            this->select_by_rectangle(*m_rectangle);
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

void SlaGizmoBase::render_scene(Render::CommandBuffer& cmd_buffer)
{
    this->render_rectangle(cmd_buffer);
}

void SlaGizmoBase::render_rectangle(Render::CommandBuffer& cmd_buffer)
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
