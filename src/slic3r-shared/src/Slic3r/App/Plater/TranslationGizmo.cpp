#include "Slic3r/App/Plater/TranslationGizmo.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Plater/GizmoNodeTag.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <numbers>

#include "Slic3r/Domain/Color.hpp"


namespace Slic3r::App::Plater {

using Domain::ColorRGBA;
using Domain::SquareMatrix4d;
using Domain::Transform3d;
using Domain::Vec3d;

constexpr double HALF_PI = 0.5 * std::numbers::pi;
static const Vec3d HANDLE_CONE_SIZE = { 10.0, 10.0, 15.0 };
constexpr double HANDLE_STEM_LENGTH = 50.0;
static const Vec3d HANDLE_CONE_OFFSET = { 0.0, 0.0, HANDLE_STEM_LENGTH };

static Transform3d axis_transform(AxisType axis)
{
    Transform3d ret = Transform3d::Identity();
    switch (axis)
    {
    case AxisType::XAxis:
    {
        ret.rotate(Eigen::AngleAxisd(HALF_PI, Vec3d::UnitY()));
        break;
    }
    case AxisType::YAxis:
    {
        ret.rotate(Eigen::AngleAxisd(-HALF_PI, Vec3d::UnitX()));
        break;
    }
    default:
    case AxisType::ZAxis:
    {
        // no rotation applied
        break;
    }
    }
    return ret;
}

static void build_handle_nodes(
    AxisType axis,
    Scene::NodeBuilder& builder,
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory
)
{
    ColorRGBA color = axis_color(axis);

    builder.set_debug_name(axis_string(axis));
    builder.set_tag(TranslationGizmoNodeTag{ axis });

    builder.child([&](Scene::NodeBuilder& bldr) {
        Render::Material material = Render::Material{}
            .set_shader(device.context().shader_manager().shader("flat"))
            .set_uniform("uniform_color", color);

        bldr
            .set_debug_name("stem")
            .set_tag(TranslationGizmoNodeTag{ axis })
            .set_mesh(data_factory.geometry(Scene::GeometryDataId::Segment), material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
            .set_material_override(material)
            .transform([](Transform3d& xform) {
                xform.rotate(Eigen::AngleAxisd(-HALF_PI, Vec3d::UnitY()));
                xform.scale(HANDLE_STEM_LENGTH * Vec3d::UnitX());
            });
    });

    builder.child([&](Scene::NodeBuilder& bldr) {
        auto geom = data_factory.geometry(Scene::GeometryDataId::Cone);
        auto mesh = data_factory.triangle_mesh(Scene::GeometryDataId::Cone);

        Render::Material material = Render::Material{}
            .set_shader(device.context().shader_manager().shader("gouraud_light"))
            .set_uniform("uniform_color", color);

        bldr
            .set_debug_name("cone")
            .set_tag(TranslationGizmoNodeTag{ axis })
            .set_mesh(geom, material, Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles))
            .set_material_override(material)
            .set_aabb(mesh->aabb_mesh())
            .transform([](Transform3d& xform) {
                xform
                    .translate(HANDLE_CONE_OFFSET)
                    .scale(HANDLE_CONE_SIZE);
            });
    });
}

TranslationGizmo::TranslationGizmo(
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    PlaterScenePresenter& scene_provider,
    Biz::ProjectInteractor& project_interactor
) :
    m_device(device),
    m_data_factory(data_factory),
    m_scene_provider(scene_provider),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_project_interactor(project_interactor),
    m_projects(project_interactor),
    m_on_scene_selection_changed_scope(m_scene_interactor, *this)
{
}

void TranslationGizmo::on_cycle_prepare()
{
    m_projects.selected().dragging = false;
}

Scene::GizmoActivationState TranslationGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    ProjectContext& project_context{m_projects.selected()};
    const auto event_type = ctx.mouse_event().type();
    if (event_type != Platform::MouseEvent::Type::ButtonDown &&
        event_type != Platform::MouseEvent::Type::Move &&
        event_type != Platform::MouseEvent::Type::ButtonUp) {
        project_context.dragging = false;
        return Scene::GizmoActivationState::Inactive;
    }

    const auto& pick_ray = ctx.pick_ray();

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
            m_scene_interactor.selection_bounding_box()
        };
        const Scene::Node* node = ctx.pick_result_node_with_tag_of_type<TranslationGizmoNodeTag>();
        if (node == nullptr || !selection_bounding_box) {
            project_context.dragging = false;
            return Scene::GizmoActivationState::Inactive;
        }

        const Biz::Scene::OrientedBoundingBox& obb{selection_bounding_box->oriented_bounding_box()};

        const TranslationGizmoNodeTag& tag = *node->tag_of_type<TranslationGizmoNodeTag>();
        Transform3d transform{Transform3d::Identity()};
        transform.translate(obb.center);
        transform.rotate(obb.rotation);
        project_context.translation_ray
            .origin = transform.translation();
        project_context.translation_ray.direction = transform.rotation() * tag.primary_axis_dir();
    }

    double t;
    if (!project_context.translation_ray.closest_point_from_ray(pick_ray, t)) {
        project_context.dragging = false;
        return Scene::GizmoActivationState::Inactive;
    }

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        project_context.start_t = t;
        project_context.dragging = true;
        return Scene::GizmoActivationState::Active;
    }

    if (!project_context.dragging)
        return Scene::GizmoActivationState::Inactive;

    Vec3d delta = project_context.translation_ray.point_at(t) - project_context.translation_ray.point_at(project_context.start_t);

    SquareMatrix4d translation_matrix = SquareMatrix4d::Identity();
    translation_matrix.col(3).head(3) = delta;

    m_scene_interactor
        .transform_selection(translation_matrix, project_context.xform_memento);

    if (event_type == Platform::MouseEvent::Type::ButtonUp) {
        m_scene_interactor.finalize_transform_selection(
            project_context.xform_memento,
            false
        );
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Translate);
        project_context.dragging = false;
        clear_highlight();
        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState::Active;
}

static void enable_all_nodes(Scene::Node* handle_nodes)
{
    if (handle_nodes != nullptr) {
        visit(*handle_nodes, [](Scene::Node& node) { node.set_enabled(true); }, true);
    }
}

static void hide_z_axis(Scene::Node& handles_node)
{
    visit(
        handles_node,
        [](Scene::Node& node)
        {
            const auto tag{node.tag_of_type<TranslationGizmoNodeTag>()};
            if (!tag) {
                return;
            }
            node.set_enabled(tag->primary_axis != AxisType::ZAxis);
        },
        true
    );
}

void TranslationGizmo::clear_highlight()
{
    ProjectContext& project_context{m_projects.selected()};
    if (project_context.highlighted) {
        Scene::Node* handle_nodes{get_handle_nodes()};
        if (handle_nodes == nullptr) {
            return;
        }
        enable_all_nodes(handle_nodes);
        if (m_scene_interactor.object_selection().contains_wipe_tower()) {
            hide_z_axis(*handle_nodes);
        }
    }
    project_context.highlighted = false;
}

void TranslationGizmo::on_transient_mouse(Scene::GizmoEventContext& ctx)
{
    ProjectContext& project_context{m_projects.selected()};
    if (!project_context.activated || project_context.dragging)
        return;
    auto* n = ctx.pick_result_node_with_tag_of_type<TranslationGizmoNodeTag>();
    if (n == nullptr) {
        clear_highlight();
    } else {
        // when hovering over a handle
        // show only the correspondent axis
        auto* p = n->parent();  // {} axis
        auto* gp = p->parent(); // main
        for (auto& child : gp->children()){
            child->set_enabled(child.get() == p);
        }
        project_context.highlighted = true;
    }
}

void TranslationGizmo::on_activated()
{
    m_projects.selected().activated = true;
    m_window->on_activated();
    Scene::Scene& scene{m_scene_provider.scene()};

    auto node{generate_handle_nodes()};
    Scene::Node* handles_node{node.get()};

    scene.add_child(node.release(), &m_scene_provider.selection_root());

    if (m_scene_interactor.object_selection().contains_wipe_tower()) {
        hide_z_axis(*handles_node);
    }
}

void TranslationGizmo::on_deactivated()
{
    m_projects.selected().activated = false;
    m_window->on_deactivated();
    m_scene_provider.clear_selection_root_children();
}

void TranslationGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
) {
    if (!enabled() || !m_projects.selected().activated) {
        return;
    }
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }
    Scene::Node* handles_node{get_handle_nodes()};
    if (!handles_node) {
        return;
    }
    if (selection.contains_wipe_tower()) {
        hide_z_axis(*handles_node);
    } else {
        enable_all_nodes(get_handle_nodes());
    }
}

bool TranslationGizmo::enabled() const {
    return !m_scene_interactor.object_selection().empty();
}

std::unique_ptr<GizmoWindow> TranslationGizmo::release_ui_window()
{
    auto window{std::make_unique<TranslationDialog>(
        m_scene_provider,
        m_project_interactor
    )};
    m_window = window.get();
    return window;
}

std::unique_ptr<Scene::Node> TranslationGizmo::generate_handle_nodes() const {
    auto& scene = m_scene_provider.scene();

    // builds the following hierarchy of elements:
    // [main] - [X axis]
    //        - [Y axis]
    //        - [Z axis]
    // each of the {} axis elements is composed by the elements:
    // [{} axis] - [stem]
    //           - [cone]

    Scene::NodeBuilder builder{ scene };
    builder.set_debug_name("main");
    builder.set_tag(TranslationGizmoNodeTag{ AxisType::None });

    builder.child([&](Scene::NodeBuilder& bldr) {
        build_handle_nodes(AxisType::XAxis, bldr, m_device, m_data_factory);
        bldr.transform([](Transform3d& xform) {
            xform = axis_transform(AxisType::XAxis) * xform;
        });
    });

    builder.child([&](Scene::NodeBuilder& bldr) {
        build_handle_nodes(AxisType::YAxis, bldr, m_device, m_data_factory);
        bldr.transform([](Transform3d& xform) {
            xform = axis_transform(AxisType::YAxis) * xform;
        });
    });

    builder.child([&](Scene::NodeBuilder& bldr) {
        build_handle_nodes(AxisType::ZAxis, bldr, m_device, m_data_factory);
    });

    return builder.build();
}

Scene::Node* TranslationGizmo::get_handle_nodes() const {
    Scene::Scene& scene{m_scene_provider.scene()};
    Scene::Node::NodeList nodes;
    scene.root().query(
        [&](const Scene::Node* node)
        {
            const auto tag{node->tag_of_type<TranslationGizmoNodeTag>()};
            if (tag == nullptr) {
                return false;
            }
            return tag->primary_axis == AxisType::None;
        },
        nodes
    );
    if (nodes.empty()) {
        return nullptr;
    }
    ASSERT(nodes.size() == 1);
    return nodes.front();
}
}
