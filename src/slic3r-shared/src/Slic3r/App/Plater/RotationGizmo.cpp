#include "Slic3r/App/Plater/RotationGizmo.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"

#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/App/Plater/RotationDialog.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"

#include "Slic3r/Math.hpp"

#include <numbers>

using Slic3r::Domain::ColorRGBA;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::X;
using Slic3r::Domain::Y;

using Slic3r::Biz::Algorithms::Point::to_2d;

namespace Slic3r::App::Plater {

namespace {

constexpr double HALF_PI            = 0.5 * std::numbers::pi;
constexpr double TWO_PI             = 2.0 * std::numbers::pi;
constexpr double CIRCLE_RADIUS      = 70.0;
constexpr double CIRCLE_DIAMETER    = 2.0 * CIRCLE_RADIUS;
static const Vec3d HANDLE_CUBE_SIZE = {10.0, 10.0, 10.0};
static const Vec3d HANDLE_CONE_SIZE = {10.0, 10.0, 15.0};
static const double HANDLE_STEM_LENGTH =
    CIRCLE_RADIUS * Scene::CIRCLE_FINE_GRADE_PRIMARY_OUT_RADIUS + 0.5 * HANDLE_CUBE_SIZE[X];
constexpr double HANDLE_GAP_LENGTH    = 1.0;
static const Vec3d HANDLE_CUBE_OFFSET = {HANDLE_STEM_LENGTH, 0.0, 0.0};
static const Vec3d HANDLE_CONE_CCW_OFFSET =
    {HANDLE_STEM_LENGTH, 0.5 * HANDLE_CUBE_SIZE[Y] + HANDLE_GAP_LENGTH, 0.0};
static const Vec3d HANDLE_CONE_CW_OFFSET =
    {HANDLE_STEM_LENGTH, -(0.5 * HANDLE_CUBE_SIZE[Y] + HANDLE_GAP_LENGTH), 0.0};

} // namespace

static Transform3d axis_transform(AxisType axis)
{
    Transform3d ret = Transform3d::Identity();
    switch (axis) {
    case AxisType::XAxis: {
        ret.rotate(Eigen::AngleAxisd(HALF_PI, Vec3d::UnitY()));
        ret.rotate(Eigen::AngleAxisd(-HALF_PI, Vec3d::UnitZ()));
        break;
    }
    case AxisType::YAxis: {
        ret.rotate(Eigen::AngleAxisd(-HALF_PI, Vec3d::UnitZ()));
        ret.rotate(Eigen::AngleAxisd(-HALF_PI, Vec3d::UnitY()));
        break;
    }
    default:
    case AxisType::ZAxis: {
        // no rotation applied
        break;
    }
    }
    return ret;
}

static Vec3d mouse_position_in_local_plane(
    AxisType axis,
    const Transform3d& orient_matrix,
    const Vec3d& center,
    const Domain::Line3d& mouse_ray
)
{
    Transform3d m = axis_transform(axis).inverse();
    m             = m * Domain::Transformation(orient_matrix).get_matrix_no_offset().inverse();

    m.translate(-center);

    const Domain::Line3d local_mouse_ray = Biz::Algorithms::Line::transformed(mouse_ray, m);
    if (std::abs(local_mouse_ray.vector().dot(Vec3d::UnitZ())) < Domain::EPSILON) {
        // if the ray is parallel to the plane containing the circle
        if (std::abs(local_mouse_ray.vector().dot(Vec3d::UnitY())) > 1.0 - Domain::EPSILON)
            // if the ray is parallel to handle direction
            return Vec3d::UnitX();
        else {
            const Vec3d world_pos =
                (local_mouse_ray.a.x() >= 0.0) ? mouse_ray.a - center : mouse_ray.b - center;
            m.translate(center);
            return m * world_pos;
        }
    } else
        return Biz::Algorithms::Line::intersect_plane(local_mouse_ray, 0.0);
}

static Vec3d extract_position(const App::Scene::Transform& xform)
{
    return xform.matrix().block<3, 1>(0, 3);
}

static void build_rotate_node(
    AxisType axis,
    Scene::NodeBuilder& builder,
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    bool use_graded_circle
)
{
    ColorRGBA color = axis_color(axis);

    builder.set_debug_name(axis_string(axis));
    builder.set_tag(RotationGizmoNodeTag{axis});

    if (use_graded_circle) {
        builder.child(
            [&](Scene::NodeBuilder& bldr)
            {
                Render::Material material =
                    Render::Material{}
                        .set_shader(device.context().shader_manager().shader("flat"))
                        .set_uniform("uniform_color", ColorRGBA::WHITE());

                bldr.set_debug_name("graded circle")
                    .set_tag(RotationGizmoNodeTag{axis})
                    .set_mesh(
                        data_factory.geometry(Scene::GeometryDataId::GradedCircle),
                        material,
                        Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                    )
                    .transform([](Transform3d& xform)
                               { xform.scale(CIRCLE_DIAMETER * Vec3d::Ones()); });
            }
        );
    } else {
        builder.child(
            [&](Scene::NodeBuilder& bldr)
            {
                Render::Material material =
                    Render::Material{}
                        .set_shader(device.context().shader_manager().shader("flat"))
                        .set_uniform("uniform_color", color);

                bldr.set_debug_name("circle")
                    .set_tag(RotationGizmoNodeTag{axis})
                    .set_mesh(
                        data_factory.geometry(Scene::GeometryDataId::Circle),
                        material,
                        Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                    )
                    .transform([](Transform3d& xform)
                               { xform.scale(CIRCLE_DIAMETER * Vec3d::Ones()); });
            }
        );
    }

    builder.child(
        [&](Scene::NodeBuilder& bldr)
        {
            bldr.set_debug_name("handle").set_tag(RotationGizmoNodeTag{axis, true});

            bldr.child(
                [&](Scene::NodeBuilder& child_bldr)
                {
                    Render::Material material =
                        Render::Material{}
                            .set_shader(device.context().shader_manager().shader("flat"))
                            .set_uniform("uniform_color", color);

                    child_bldr.set_debug_name("stem")
                        .set_tag(RotationGizmoNodeTag{axis})
                        .set_mesh(
                            data_factory.geometry(Scene::GeometryDataId::Segment),
                            material,
                            Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                        )
                        .transform([](Transform3d& xform)
                                   { xform.scale(HANDLE_STEM_LENGTH * Vec3d::UnitX()); });
                }
            );

            bldr.child(
                [&](Scene::NodeBuilder& child_bldr)
                {
                    auto geom = data_factory.geometry(Scene::GeometryDataId::Cube);
                    auto mesh = data_factory.triangle_mesh(Scene::GeometryDataId::Cube);

                    Render::Material material =
                        Render::Material{}
                            .set_shader(device.context().shader_manager().shader("gouraud_light"))
                            .set_uniform("uniform_color", color);

                    child_bldr.set_debug_name("cube")
                        .set_tag(RotationGizmoNodeTag{axis})
                        .set_mesh(
                            geom,
                            material,
                            Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                        )
                        .set_aabb(mesh->aabb_mesh())
                        .transform(
                            [](Transform3d& xform)
                            { xform.translate(HANDLE_CUBE_OFFSET).scale(HANDLE_CUBE_SIZE); }
                        );
                }
            );

            bldr.child(
                [&](Scene::NodeBuilder& child_bldr)
                {
                    auto geom = data_factory.geometry(Scene::GeometryDataId::Cone);
                    auto mesh = data_factory.triangle_mesh(Scene::GeometryDataId::Cone);

                    Render::Material material =
                        Render::Material{}
                            .set_shader(device.context().shader_manager().shader("gouraud_light"))
                            .set_uniform("out_of_bed_threshold_z", -FLT_MAX)
                            .set_uniform("uniform_color", color);

                    child_bldr.set_debug_name("cone ccw")
                        .set_tag(RotationGizmoNodeTag{axis})
                        .set_mesh(
                            geom,
                            material,
                            Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                        )
                        .set_aabb(mesh->aabb_mesh())
                        .transform(
                            [](Transform3d& xform)
                            {
                                xform.translate(HANDLE_CONE_CCW_OFFSET)
                                    .rotate(Eigen::AngleAxisd{-HALF_PI, Vec3d::UnitX()})
                                    .scale(HANDLE_CONE_SIZE);
                            }
                        );
                }
            );

            bldr.child(
                [&](Scene::NodeBuilder& child_bldr)
                {
                    auto geom = data_factory.geometry(Scene::GeometryDataId::Cone);
                    auto mesh = data_factory.triangle_mesh(Scene::GeometryDataId::Cone);

                    Render::Material material =
                        Render::Material{}
                            .set_shader(device.context().shader_manager().shader("gouraud_light"))
                            .set_uniform("uniform_color", color);

                    child_bldr.set_debug_name("cone cw")
                        .set_tag(RotationGizmoNodeTag{axis})
                        .set_mesh(
                            geom,
                            material,
                            Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                        )
                        .set_aabb(mesh->aabb_mesh())
                        .transform(
                            [](Transform3d& xform)
                            {
                                xform.translate(HANDLE_CONE_CW_OFFSET)
                                    .rotate(Eigen::AngleAxisd{HALF_PI, Vec3d::UnitX()})
                                    .scale(HANDLE_CONE_SIZE);
                            }
                        );
                }
            );
        }
    );
}

static void build_main_node(
    const std::string& debug_name,
    bool use_graded_circle,
    Scene::NodeBuilder& builder,
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory
)
{
    builder.set_debug_name(debug_name);
    builder.set_tag(RotationGizmoNodeTag{AxisType::None});

    builder.child(
        [&](Scene::NodeBuilder& bldr)
        {
            build_rotate_node(AxisType::XAxis, bldr, device, data_factory, use_graded_circle);
            bldr.transform([](Transform3d& xform)
                           { xform = axis_transform(AxisType::XAxis) * xform; });
        }
    );

    builder.child(
        [&](Scene::NodeBuilder& bldr)
        {
            build_rotate_node(AxisType::YAxis, bldr, device, data_factory, use_graded_circle);
            bldr.transform([](Transform3d& xform)
                           { xform = axis_transform(AxisType::YAxis) * xform; });
        }
    );

    builder.child(
        [&](Scene::NodeBuilder& bldr)
        { build_rotate_node(AxisType::ZAxis, bldr, device, data_factory, use_graded_circle); }
    );
}

RotationGizmo::RotationGizmo(
    Render::Device& device,
    Scene::GeometryDataFactory& data_factory,
    PlaterScenePresenter& scene_presenter,
    Biz::ProjectInteractor& project_interactor
) :
    m_device(device),
    m_data_factory(data_factory),
    m_scene_presenter(scene_presenter),
    m_project_interactor(project_interactor),
    m_scene_interactor(project_interactor.scene_interactor()),
    m_projects(project_interactor)
{
    m_scene_presenter.add_listener<ISelectionExtentsChangedListener>(this);
    m_scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

RotationGizmo::Snap RotationGizmo::m_snap = {
    {CIRCLE_RADIUS * Scene::CIRCLE_COARSE_GRADE_IN_RADIUS,
     CIRCLE_RADIUS* Scene::CIRCLE_COARSE_GRADE_OUT_RADIUS},
    {CIRCLE_RADIUS, CIRCLE_RADIUS* Scene::CIRCLE_FINE_GRADE_PRIMARY_OUT_RADIUS}
};

RotationGizmo::~RotationGizmo()
{
    m_scene_presenter.remove_listener<ISelectionExtentsChangedListener>(this);
    m_scene_interactor.remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
}

Scene::GizmoActivationState RotationGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    ProjectContext& project_context{m_projects.selected()};

    const auto event_type = ctx.mouse_event().type();
    if (event_type != Platform::MouseEvent::Type::ButtonDown
        && event_type != Platform::MouseEvent::Type::Move
        && event_type != Platform::MouseEvent::Type::ButtonUp)
    {
        on_stop_dragging();
        return Scene::GizmoActivationState::Inactive;
    }

    const auto& pick_ray = ctx.pick_ray();

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        const Scene::Node* node = ctx.pick_result_node_with_tag_of_type<RotationGizmoNodeTag>();
        if (node == nullptr) {
            on_stop_dragging();
            return Scene::GizmoActivationState::Inactive;
        }

        const RotationGizmoNodeTag& tag = *node->tag_of_type<RotationGizmoNodeTag>();
        project_context.translation_ray.origin =
            extract_position(m_scene_presenter.selection_root().world_transform());
        project_context.translation_ray.direction = tag.primary_axis_dir();
    }

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
            m_scene_interactor.selection_bounding_box()
        };

        if (!selection_bounding_box) {
            on_stop_dragging();
            return Scene::GizmoActivationState::Inactive;
        }

        project_context.dragging     = true;
        project_context.start_obb    = selection_bounding_box->oriented_bounding_box();
        project_context.was_floating = selection_bounding_box->is_floating();
        Domain::Transform3d orient_matrix{Domain::Transform3d::Identity()};
        orient_matrix.rotate(project_context.start_obb.rotation);
        project_context.start_direction =
            to_2d(mouse_position_in_local_plane(
                      project_context.curr_axis,
                      orient_matrix,
                      project_context.start_obb.center,
                      Domain::Line3d(pick_ray.origin, pick_ray.point_at(10.0))
                  ))
                .normalized();

        return Scene::GizmoActivationState::Active;
    }

    if (!project_context.dragging)
        return Scene::GizmoActivationState::Inactive;

    if (project_context.curr_axis != AxisType::None) {
        Domain::Transform3d orient_matrix{Domain::Transform3d::Identity()};
        orient_matrix.rotate(project_context.start_obb.rotation);
        Vec2d pos = to_2d(mouse_position_in_local_plane(
            project_context.curr_axis,
            orient_matrix,
            project_context.start_obb.center,
            Domain::Line3d(pick_ray.origin, pick_ray.point_at(10.0))
        ));

        Vec2d new_dir = pos.normalized();

        double theta = acos(std::clamp(new_dir.dot(project_context.start_direction), -1.0, 1.0));
        if (cross2(project_context.start_direction, new_dir) < 0.0)
            theta = TWO_PI - theta;

        double len = pos.norm();

        // take in account that the selection root is scaled to keep the gizmo with constant screen size
        const App::Scene::INodeTransformModifier* modifier =
            m_scene_presenter.selection_root().transform_modifier();
        if (modifier != nullptr) {
            const App::Scene::Camera& camera = m_scene_presenter.scene().camera();
            double scale = camera.cam_projection().constant_screen_space_size_scale(
                               camera,
                               (project_context.start_obb.center - camera.position()).norm()
                           )
                * Scene::SELECTION_ROOT_SCALE_MODIFIER;
            len /= scale;
        }

        // snap to coarse snap region
        if (m_snap.coarse.in <= len && len <= m_snap.coarse.out) {
            double step = TWO_PI / Scene::CIRCLE_COARSE_GRADE_STEPS;
            theta       = step * std::round(theta / step);
        } else {
            // snap to fine snap region
            if (m_snap.fine.in <= len && len <= m_snap.fine.out) {
                double step = TWO_PI / Scene::CIRCLE_FINE_GRADE_SECONDARY_STEPS;
                theta       = step * std::round(theta / step);
            }
        }

        if (theta == TWO_PI)
            theta = 0.0;

        Domain::Vec3d rotation{Domain::Vec3d::Zero()};
        const std::optional<int> axis_index{get_axis_index(project_context.curr_axis)};
        ASSERT(axis_index);
        rotation(*axis_index) = theta;
        m_scene_interactor.transform_selection(
            get_rotation_matrix(
                project_context.start_obb.rotation,
                project_context.start_obb.center,
                rotation
            ),
            project_context.xform_memento,
            false
        );
        Transform3d local_rotation{Transform3d::Identity()};
        local_rotation.rotate(Eigen::AngleAxisd(theta, Vec3d::UnitZ()));
        project_context.handles[*axis_index]->set_local_transform(local_rotation);
    }

    if (event_type == Platform::MouseEvent::Type::ButtonUp) {
        m_scene_interactor.finalize_transform_selection(
            project_context.xform_memento,
            false
        );
        if (!project_context.was_floating) {
            Biz::Scene::TransformMemento memento;
            memento.forced_volume_mode = true;
            m_scene_interactor
                .transform_selection(Domain::SquareMatrix4d::Identity(), memento, true);
        }
        on_stop_dragging();

        m_project_interactor.undo_provider().take_snapshot(
            Biz::UndoSnapshotType::Rotate
        );

        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState::Active;
}

void RotationGizmo::on_transient_mouse(Scene::GizmoEventContext& ctx)
{
    ProjectContext& project_context{m_projects.selected()};
    if (!project_context.activated || project_context.dragging) {
        return;
    }

    const auto* node{ctx.pick_result_node_with_tag_of_type<RotationGizmoNodeTag>()};
    if (node == nullptr) {
        remove_highlight_node();
    } else {
        add_highlight_node(node->parent()->tag_of_type<RotationGizmoNodeTag>()->primary_axis);
    }
}

void RotationGizmo::on_cycle_prepare()
{
    m_projects.selected().dragging = false;
}

static void hide_xy_axis(Scene::Node& main_node)
{
    visit(
        main_node,
        [](Scene::Node& node)
        {
            const auto tag{node.tag_of_type<RotationGizmoNodeTag>()};
            if (!tag) {
                return;
            }
            node.set_enabled(
                tag->primary_axis != AxisType::XAxis && tag->primary_axis != AxisType::YAxis
            );
        },
        true
    );
}

void RotationGizmo::on_activated()
{
    ProjectContext& project_context{m_projects.selected()};
    project_context.activated = true;
    m_window->on_activated(m_project_interactor.selected_project_id());

    auto& scene{m_scene_presenter.scene()};

    Scene::NodeBuilder builder{scene};
    build_main_node("main", false, builder, m_device, m_data_factory);
    auto node{builder.build()};
    project_context.main_node = node.get();
    scene.add_child(node.release(), &m_scene_presenter.selection_root());

    if (m_scene_interactor.object_selection().contains_wipe_tower()) {
        hide_xy_axis(*project_context.main_node);
    }
}

void RotationGizmo::on_deactivated()
{
    ProjectContext& project_context{m_projects.selected()};
    remove_highlight_node();
    project_context.activated = false;
    project_context.main_node = nullptr;
    m_window->on_deactivated();
    m_scene_presenter.clear_selection_root_children();

}

bool RotationGizmo::enabled() const
{
    return !m_scene_interactor.object_selection().empty();
}

void RotationGizmo::on_scene_selection_bounding_box_changed(
    Domain::SelectionId project_id,
    const std::optional<Biz::Scene::SelectionExtents>&
)
{
    ProjectContext& project_context{m_projects.selected()};
    if (project_context.activated && project_context.dragging) {
        m_scene_presenter.selection_root().set_enabled(false);
    }
}

static void enable_all_nodes(Scene::Node& main_node)
{
    visit(main_node, [](Scene::Node& node) { node.set_enabled(true); }, true);
}

void RotationGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    ProjectContext& project_context{m_projects.selected()};
    if (!enabled() || !m_projects.selected().activated) {
        return;
    }
    if (project_id != m_project_interactor.selected_project_id()) {
        return;
    }
    Scene::Node* handles_node{project_context.main_node};
    if (!handles_node) {
        return;
    }
    if (selection.contains_wipe_tower()) {
        hide_xy_axis(*handles_node);
    } else {
        enable_all_nodes(*handles_node);
    }
}

std::unique_ptr<GizmoWindow> RotationGizmo::release_ui_window()
{
    auto window{std::make_unique<RotationDialog>(m_scene_presenter, m_project_interactor)};
    m_window = window.get();
    return window;
}

void RotationGizmo::on_stop_dragging()
{
    remove_highlight_node();
    m_projects.selected().dragging = false;
}

void RotationGizmo::add_highlight_node(AxisType axis)
{
    ProjectContext& project_context{m_projects.selected()};
    if (project_context.highlight_node != nullptr) {
        return;
    }
    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_scene_interactor.selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return;
    }
    const Biz::Scene::OrientedBoundingBox& obb{selection_bounding_box->oriented_bounding_box()};

    Scene::Scene& scene{m_scene_presenter.scene()};
    Scene::NodeBuilder builder{scene};
    builder.set_screen_space_sized_modifier(Scene::SELECTION_ROOT_SCALE_MODIFIER);
    build_main_node("dragging", true, builder, m_device, m_data_factory);
    auto node{builder.build()};
    project_context.highlight_node = node.get();
    scene.add_child(node.release());

    project_context.highlight_node->query(
        [](const Scene::Node* n) -> bool
        {
            const RotationGizmoNodeTag* tag = n->tag_of_type<RotationGizmoNodeTag>();
            return (tag != nullptr && tag->is_handle);
        },
        project_context.handles,
        true
    );

    Transform3d world_transform{Domain::Transform3d::Identity()};
    world_transform.translate(obb.center);
    world_transform.rotate(obb.rotation);
    project_context.highlight_node->set_world_transform(world_transform);
    project_context.curr_axis = axis;
    for (auto& child : project_context.highlight_node->children()) {
        auto tag{child->tag_of_type<RotationGizmoNodeTag>()};
        ASSERT(tag != nullptr);
        child->set_enabled(tag->primary_axis == project_context.curr_axis);
    }
    m_scene_presenter.selection_root().set_enabled(false);
}

void RotationGizmo::remove_highlight_node()
{
    ProjectContext& project_context{m_projects.selected()};
    if (project_context.highlight_node == nullptr) {
        return;
    }
    m_scene_presenter.selection_root().set_enabled(true);
    Scene::Scene& scene{m_scene_presenter.scene()};
    const bool removed{scene.remove_child(project_context.highlight_node)};
    ASSERT(removed);
    project_context.handles        = {};
    project_context.curr_axis      = AxisType::None;
    project_context.highlight_node = nullptr;
}

} // namespace Slic3r::App::Plater
