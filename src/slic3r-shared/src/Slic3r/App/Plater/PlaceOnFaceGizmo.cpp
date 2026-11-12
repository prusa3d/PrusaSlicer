#include "Slic3r/App/Plater/PlaceOnFaceGizmo.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"
#include "Slic3r/App/Scene/NodeVisitor.hpp"
#include "Slic3r/App/Scene/SceneNodeTag.hpp"

#include "Slic3r/Domain/ModelVolume.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <Slic3r/App/Render/GeometryBuilder.hpp>

#include "PlaceOnFaceGizmoPlanes.hpp"

namespace Slic3r::App::Plater {

struct POFNodeTag
{
    const size_t id;
};

using Slic3r::Domain::ColorRGBA;

static const ColorRGBA PLANE_HOVERED_COLOR = ColorRGBA(0.95f, 0.95f, 0.95f, 0.5f);
static const ColorRGBA PLANE_DEFAULT_COLOR = ColorRGBA(0.75f, 0.75f, 0.75f, 0.5f);

PlaceOnFaceGizmo::PlaceOnFaceGizmo(
    Render::Device& device,
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor
) :
    m_device(device),
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor())
{
    m_scene_presenter.scene().add_listener<ISceneChangedListener>(this);
}

void PlaceOnFaceGizmo::on_activated()
{
    recreate_planes_and_nodes();
    m_is_active = true;

    m_scene_presenter.scene().add_listener<IThumbnailRenderListener>(this);
}

void PlaceOnFaceGizmo::on_deactivated()
{
    destroy_planes_and_nodes();
    m_is_active = false;

    m_scene_presenter.scene().remove_listener<IThumbnailRenderListener>(this);
}

void PlaceOnFaceGizmo::on_thumbnail_render_begin()
{
    // Before rendering a thumbnail, hide gizmo nodes.
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(false);
    }
}

void PlaceOnFaceGizmo::on_thumbnail_render_end()
{
    // After rendering a thumbnail, restore gizmo nodes.
    if (m_main_node != nullptr) {
        m_main_node->set_enabled(true);
    }
}

void PlaceOnFaceGizmo::on_project_activated(size_t new_project_id)
{
    on_activated();
}

void PlaceOnFaceGizmo::on_project_deactivated(size_t old_project_id)
{
    on_deactivated();
}

void PlaceOnFaceGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    if (m_is_active && enabled()) {
        // TODO: An optimization is possible. If the user switched from one instance
        // to another one belonging to the same object, it is not necessary to
        // recalculate planes - hanging the existing nodes in scene graph
        // under a different parent should be enough.
        recreate_planes_and_nodes();
    }
}

void PlaceOnFaceGizmo::on_scene_selection_transformed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    if (m_is_active) {
        // We should recreate everything if scaling or relative rotation
        // of volumes changed. However, let's now rely on the fact
        // that none of these cases can happen while the gizmo is active.
    }
}

void PlaceOnFaceGizmo::on_node_removed(Scene::Node* node)
{
    if (node == m_main_node)
        m_main_node = nullptr;
}

bool PlaceOnFaceGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection =
        m_project_interactor.scene_interactor().object_selection();
    return selection.state() == Biz::Scene::SelectionState::WholeInstance;
}

void PlaceOnFaceGizmo::build_plane_node(Scene::NodeBuilder& bldr, indexed_triangle_set&& mesh_its, size_t plane_id)
{
    if (mesh_its.empty()) {
        return;
    }

    Scene::AuxiliaryElementId id{Scene::AuxiliaryElementId::Type::AABB, plane_id};

    const auto& trimesh = m_model_triangle_mesh_manager.get_or_create(
        id,
        [&]() -> std::unique_ptr<Scene::TriangleMesh>
        { return std::make_unique<Scene::TriangleMesh>(std::move(mesh_its)); }
    );
    const auto* geom = m_model_geometry_manager.get_or_create(
        id,
        [&]() { return Render::geometry_from_triangle_mesh(m_device, trimesh->triangles()); }
    );

    auto material = Render::Material{}
                        .set_shader(m_device.context().shader_manager().shader("flat"))
                        .set_uniform("uniform_color", PLANE_DEFAULT_COLOR)
                        .set_transparent(PLANE_DEFAULT_COLOR.is_transparent());

    bldr.set_debug_name(fmt::format("FacePlane :id {}", plane_id))
        .set_tag(POFNodeTag{plane_id})
        .set_mesh(geom, material, int(0))
        .set_aabb(trimesh->aabb_mesh())
        .set_material_override(material);
}

void PlaceOnFaceGizmo::destroy_planes_and_nodes()
{
    if (m_main_node)
        m_scene_presenter.scene().remove_child(m_main_node);
    m_main_node = nullptr;

    m_model_geometry_manager.release_all();
    m_model_triangle_mesh_manager.release_all();
    m_normals_and_points = {};
}


void PlaceOnFaceGizmo::recreate_planes_and_nodes()
{
    destroy_planes_and_nodes();

    const Biz::Scene::ObjectSelection& selection = m_scene_interactor.object_selection();

    if (selection.elements.size() != 1
        || selection.mode != Slic3r::Biz::Scene::SelectionMode::Instance)
    {
        // We can't generate faces for multiple objects simultaneously.
        return;
    }

    Domain::Project& project          = m_project_interactor.selected_project();
    const Domain::ElementRef& element = selection.elements.front();
    ASSERT(element.volume_id == 0); // Whole object is selected

    Domain::ModelObject* new_object = project.find_object_by_id(element.object_id);
    size_t first_vol_id = new_object->volumes.front()->id().id;
    
    auto& scene = m_scene_presenter.scene();
    
    Scene::visit_conditional(scene.root(), [&](Scene::Node& n) {
        const Scene::SceneNodeTag* t = n.tag_of_type<Scene::SceneNodeTag>();
        if (t != nullptr && t->instance_id == element.instance_id
            && t->object_id == element.object_id && t->volume_id == first_vol_id) {
            // This is the first volume of this instance. Let's create
            // a subnode which will become a parent of our planes.
            Scene::NodeBuilder builder{scene};
            builder.set_debug_name("PlaceOnFace_main");
            builder.set_tag(POFNodeTag{});
            m_main_node = builder.build().release();
            scene.add_child(m_main_node, &n);
            return false;
        }
        return true;
    });

    std::vector<PlaneData> planes = calculate_planes(*new_object);

    // Generated planes are returned in instance coordinates. However, we need to have
    // them in volume coordinates so the gizmo can change both instance and volume matrix.
    Domain::Transform3f trafo = new_object->volumes.front()->get_transformation().get_matrix_no_offset().cast<float>();
    Domain::Transform3f trafo_inv = new_object->volumes.front()->get_transformation().get_matrix().inverse().cast<float>();

    for (PlaneData& plane : planes) {
        for (Domain::Vec3f& pt : plane.its.vertices)
            pt = trafo_inv * pt;
        plane.normal = trafo.inverse().matrix().block(0,0,3,3).inverse().transpose() * plane.normal;
    }

    // All is ready. Save the normal and one point belonging to each of the planes
    // and build the respective scene graph node.
    for (size_t i = 0; i < planes.size(); ++i) {
        m_normals_and_points.push_back({planes[i].normal.cast<double>(), planes[i].its.vertices.front().cast<double>()});
        Scene::NodeBuilder builder(scene);
        build_plane_node(builder, std::move(planes[i].its), i);
        scene.add_child(builder.build().release(), m_main_node);
    }
}

std::array<Domain::Vec3d, 2> PlaceOnFaceGizmo::plane_to_world_coordinates(size_t plane_id) const
{
    ASSERT(plane_id < m_normals_and_points.size());
    return plane_to_world_coordinates(m_normals_and_points[plane_id][0], m_normals_and_points[plane_id][1]);
}

std::array<Domain::Vec3d, 2>
PlaceOnFaceGizmo::plane_to_world_coordinates(const Domain::Vec3d& direction, const Domain::Vec3d& point) const
{
    const Domain::ElementRef& element = m_scene_interactor.object_selection().elements.front();
    ASSERT(element.volume_id == 0); // Whole object is selected

    const Domain::Project& project = m_project_interactor.selected_project();
    const Domain::ModelInstance* instance = project.find_instance_by_id(element.object_id, element.instance_id);
    const Domain::ModelObject* object = project.find_object_by_id(element.object_id);
    const Domain::ModelVolume* volume = object->volumes.front();

    Domain::Transform3d inst_trafo = instance->get_transformation().get_matrix_no_offset();
    Domain::Transform3d vol_trafo = volume->get_transformation().get_matrix_no_offset();

    Domain::Vec3d world_normal = inst_trafo.matrix().block(0, 0, 3, 3).inverse().transpose() *
        vol_trafo.matrix().block(0, 0, 3, 3).inverse().transpose() * direction;
    Domain::Vec3d world_point = instance->get_transformation().get_matrix() *
        volume->get_transformation().get_matrix() * point;

    return { world_normal, world_point };
}

Scene::GizmoActivationState
PlaceOnFaceGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    const auto event_type   = ctx.mouse_event().type();
    const auto event_button = ctx.mouse_event().button();

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        if (event_button == Platform::MouseButton::Left) {
            Scene::Node* plane_node = ctx.pick_result_node_with_tag_of_type<POFNodeTag>();
            if (plane_node != nullptr) {
                POFNodeTag* tag = plane_node->tag_of_type<POFNodeTag>();
                ASSERT(tag != nullptr && tag->id < m_normals_and_points.size());
                // Rotates only if the plane is facing the camera.
                // This prevents from rotating when the user clicks on a plane which is invisible.
                if (plane_to_world_coordinates(tag->id)[0].dot(ctx.pick_ray().direction) < 0.0) {
                    rotate_selection(m_normals_and_points[tag->id][0], m_normals_and_points[tag->id][1]);
                    return Scene::GizmoActivationState::Active;
                }
            }
        }
    }
    if (event_type == Platform::MouseEvent::Type::ButtonUp) {
        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState{};
}

void PlaceOnFaceGizmo::on_transient_mouse(Scene::GizmoEventContext& ctx)
{
    if (!m_main_node)
        return;
    const Scene::Node* node = ctx.pick_result_node_with_tag_of_type<POFNodeTag>();
    std::optional<size_t> hovered_plane_id =
        node ? std::optional<size_t>(node->tag_of_type<POFNodeTag>()->id) : std::nullopt;

    // TODO: this could probably be optimized. Instead of looping through all, we
    // could hover the new plane and unhovered the previous one (if we remembered it).
    for (const auto& node : m_main_node->children()) {
        POFNodeTag* tag = node.get()->tag_of_type<POFNodeTag>();

        ColorRGBA color = hovered_plane_id.has_value() && tag->id == hovered_plane_id.value() ?
            PLANE_HOVERED_COLOR :
            PLANE_DEFAULT_COLOR;

        Render::Material material = node.get()->render_component()->material();
        material.set_uniform("uniform_color", color).set_transparent(color.is_transparent());
        node.get()->set_material_override(material);
    }

    m_scene_presenter.set_sinking_contours_highlight_enabled(
        !hovered_plane_id.has_value() || plane_to_world_coordinates(*hovered_plane_id)[0].dot(ctx.pick_ray().direction) > 0.0
    );
}

void PlaceOnFaceGizmo::rotate_selection(const Domain::Vec3d& direction, const Domain::Vec3d& point) const
{
    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_scene_interactor.selection_bounding_box()
    };
    if (! selection_bounding_box)
        return;
    const Domain::Vec3d& center = selection_bounding_box->oriented_bounding_box().center;

    // direction and point are both in the coordinate system of the first
    // volume. Both need to be transformed into world before we can continue.
    std::array<Domain::Vec3d, 2> world_plane = plane_to_world_coordinates(direction, point);

    auto quatern = Eigen::Quaterniond{}.setFromTwoVectors(world_plane[0], -Domain::Vec3d::UnitZ());
    Eigen::Matrix3d rotation_3x3 = quatern.toRotationMatrix();
    auto tr = Domain::Transform3d::Identity();
    tr.matrix().block<3,3>(0,0) = rotation_3x3;
    tr.matrix().block<3,1>(0,3) = center - rotation_3x3 * center;

    // Finally tell scene interactor to rotate the object.
    m_scene_interactor.transform_selection(tr.matrix(), true);
    m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::PlaceOnFace);
}

} // namespace Slic3r::App::Plater
