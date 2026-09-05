#include "Slic3r/App/Plater/RotationGizmo.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Render/Device.hpp"
#include "Slic3r/App/Plater/PlaterScenePresenter.hpp"

#include "Slic3r/Biz/Algorithms/Line.hpp"
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Domain/Axis.hpp"
#include "Slic3r/Domain/Color.hpp"
#include "Slic3r/Domain/Constants.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/Transformation.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/App/Plater/RotationDialog.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"
#include "Slic3r/Biz/Emboss/TextLines.hpp"
#include "Slic3r/Biz/Emboss/TextBender.hpp"

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

            if (!use_graded_circle) {
                bldr.child(
                    [&](Scene::NodeBuilder& child_bldr)
                    {
                        auto geom = data_factory.geometry(Scene::GeometryDataId::Cube);
                        auto mesh = data_factory.triangle_mesh(Scene::GeometryDataId::Cube);

                        Render::Material material =
                            Render::Material{}
                                .set_shader(device.context().shader_manager().shader("gouraud_light"))
                                .set_uniform("uniform_color", color);

                        child_bldr.set_debug_name("bend slider cube")
                            .set_tag(RotationGizmoNodeTag{axis, false, true})
                            .set_mesh(
                                geom,
                                material,
                                Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                            )
                            .set_aabb(mesh->aabb_mesh())
                            .transform(
                                [](Transform3d& xform)
                                {
                                    xform.translate(Vec3d(0.5 * HANDLE_STEM_LENGTH, 0.0, 0.0))
                                         .scale(0.5 * HANDLE_CUBE_SIZE);
                                }
                            );
                    }
                );
            }
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
        if (tag.is_bend_slider && is_selected_volume_emboss_text()) {
            if (m_window && m_window->is_bend_locked(tag.primary_axis))
                return Scene::GizmoActivationState::Inactive;
            const auto selected_text = Biz::Emboss::get_selected_text_volume(m_project_interactor);
            if (!selected_text.volume || selected_text.volume->mesh().its.vertices.empty()
                || !m_scene_interactor.selection_bounding_box())
                return Scene::GizmoActivationState::Inactive;
            project_context.dragging_bend_slider = true;
            project_context.bend_mesh_changed = false;
            project_context.bend_slider_axis = tag.primary_axis;
            project_context.curr_axis = tag.primary_axis;

            const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
                m_scene_interactor.selection_bounding_box()
            };
            if (selection_bounding_box) {
                project_context.start_obb = selection_bounding_box->oriented_bounding_box();
            }

            if (selected_text.volume && selected_text.volume->text_configuration) {
                project_context.base_unbent_mesh = selected_text.volume->mesh().its;
                project_context.base_mesh_bbox = selected_text.volume->mesh().bounding_box();
                project_context.bend_target_element = Domain::ElementRef(
                    selected_text.volume->get_object()->id().id,
                    selected_text.instance_id,
                    selected_text.volume->id().id
                );
                const auto& prop = selected_text.volume->text_configuration->style.prop;
                float prev_h = prop.bend_horizontal.value_or(0.0f);
                float prev_v = prop.bend_vertical.value_or(0.0f);
                float prev_arc = prop.bend_arc.value_or(0.0f);
                if (std::abs(prev_h) > 1e-4f || std::abs(prev_v) > 1e-4f || std::abs(prev_arc) > 1e-4f) {
                    Biz::Emboss::BendParams prev_params{prev_h, prev_v, prev_arc};
                    if (const auto& reference = selected_text.volume->text_configuration->bend_reference)
                        Biz::Emboss::TextBender::restore_mesh(project_context.base_unbent_mesh, prev_params, *reference);
                    else
                        Biz::Emboss::TextBender::unbend_mesh(project_context.base_unbent_mesh, prev_params, project_context.base_mesh_bbox);
                }
                project_context.base_mesh_bbox = selected_text.volume->text_configuration->bend_reference
                    .value_or(Domain::bounding_box(project_context.base_unbent_mesh));
                Biz::Emboss::TextBender::prepare_mesh(project_context.base_unbent_mesh, project_context.base_mesh_bbox);
                project_context.current_bend_horizontal = static_cast<float>(Biz::Emboss::TextBender::clamp_angle(prev_h));
                project_context.current_bend_vertical = static_cast<float>(Biz::Emboss::TextBender::clamp_angle(prev_v));
                project_context.current_bend_arc = static_cast<float>(Biz::Emboss::TextBender::clamp_angle(prev_arc));
                const auto position = bend_mouse_position(ctx);
                if (!position) {
                    on_stop_dragging();
                    return Scene::GizmoActivationState::Inactive;
                }
                const float angle = tag.primary_axis == AxisType::YAxis
                    ? project_context.current_bend_horizontal : tag.primary_axis == AxisType::XAxis
                    ? project_context.current_bend_vertical : project_context.current_bend_arc;
                project_context.bend_drag_offset = HANDLE_STEM_LENGTH * (0.5 + 0.35 * angle / std::numbers::pi)
                    - *position;
            }

            return Scene::GizmoActivationState::Active;
        }

        project_context.translation_ray.origin =
            extract_position(m_scene_presenter.selection_root().world_transform());
        project_context.translation_ray.direction = tag.primary_axis_dir();
    }

    if (project_context.dragging_bend_slider) {
        if (event_type == Platform::MouseEvent::Type::Move) {
            if (const auto position = bend_mouse_position(ctx))
                apply_bend_slider_drag(*position + project_context.bend_drag_offset);
            return Scene::GizmoActivationState::Active;
        }

        if (event_type == Platform::MouseEvent::Type::ButtonUp) {
            on_stop_dragging();
            return Scene::GizmoActivationState::Done;
        }
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
    if (!project_context.activated || project_context.dragging || project_context.dragging_bend_slider) {
        return;
    }

    const auto* node{ctx.pick_result_node_with_tag_of_type<RotationGizmoNodeTag>()};
    if (node == nullptr || node->tag_of_type<RotationGizmoNodeTag>()->is_bend_slider) {
        // Bend cubes stay on the main node; highlighting must not hide them.
        remove_highlight_node();
    } else {
        add_highlight_node(node->tag_of_type<RotationGizmoNodeTag>()->primary_axis);
    }
}

void RotationGizmo::on_cycle_prepare()
{
    finish_bend_drag();
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

    project_context.bend_slider_x = nullptr;
    project_context.bend_slider_y = nullptr;
    project_context.bend_slider_z = nullptr;
    visit(
        *project_context.main_node,
        [&project_context](Scene::Node& n)
        {
            const RotationGizmoNodeTag* tag = n.tag_of_type<RotationGizmoNodeTag>();
            if (tag != nullptr && tag->is_bend_slider) {
                if (tag->primary_axis == AxisType::XAxis)
                    project_context.bend_slider_x = &n;
                else if (tag->primary_axis == AxisType::YAxis)
                    project_context.bend_slider_y = &n;
                else if (tag->primary_axis == AxisType::ZAxis)
                    project_context.bend_slider_z = &n;
            }
        },
        true
    );

    if (m_scene_interactor.object_selection().contains_wipe_tower()) {
        hide_xy_axis(*project_context.main_node);
    }
    update_bend_sliders_visibility_and_position();
}

void RotationGizmo::on_deactivated()
{
    ProjectContext& project_context{m_projects.selected()};
    on_stop_dragging();
    project_context.activated = false;
    project_context.main_node = nullptr;
    project_context.bend_slider_x = nullptr;
    project_context.bend_slider_y = nullptr;
    project_context.bend_slider_z = nullptr;
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
    if (project_id != m_project_interactor.selected_project_id())
        return;
    ProjectContext& project_context{m_projects.selected()};
    if (!project_context.activated)
        return;
    if (project_context.dragging) {
        m_scene_presenter.selection_root().set_enabled(false);
    } else if (project_context.dragging_bend_slider) {
        // Keep the stem fixed while mesh bounds move underneath it.
        Transform3d transform = Transform3d::Identity();
        transform.translate(project_context.start_obb.center);
        transform.rotate(project_context.start_obb.rotation);
        m_scene_presenter.selection_root().set_world_transform(transform);
    } else {
        update_bend_sliders_visibility_and_position();
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
    update_bend_sliders_visibility_and_position();
}

std::unique_ptr<GizmoWindow> RotationGizmo::release_ui_window()
{
    auto window{std::make_unique<RotationDialog>(m_scene_presenter, m_project_interactor)};
    m_window = window.get();
    m_window->on_bend_locks_changed = [this]() { update_bend_sliders_visibility_and_position(); };
    return window;
}

void RotationGizmo::on_stop_dragging()
{
    finish_bend_drag();
    remove_highlight_node();
    m_projects.selected().dragging = false;
}

bool RotationGizmo::is_selected_volume_emboss_text() const
{
    auto selected_text = Biz::Emboss::get_selected_text_volume(m_project_interactor);
    return selected_text.volume != nullptr && selected_text.volume->text_configuration.has_value();
}

void RotationGizmo::update_bend_sliders_visibility_and_position()
{
    ProjectContext& project_context{m_projects.selected()};
    if (project_context.dragging_bend_slider) {
        return;
    }
    const bool is_text = is_selected_volume_emboss_text();

    if (project_context.bend_slider_x != nullptr) {
        project_context.bend_slider_x->set_enabled(is_text && (!m_window || !m_window->is_bend_locked(AxisType::XAxis)));
    }
    if (project_context.bend_slider_y != nullptr) {
        project_context.bend_slider_y->set_enabled(is_text && (!m_window || !m_window->is_bend_locked(AxisType::YAxis)));
    }
    if (project_context.bend_slider_z != nullptr) {
        project_context.bend_slider_z->set_enabled(is_text && (!m_window || !m_window->is_bend_locked(AxisType::ZAxis)));
    }

    if (is_text) {
        auto selected_text = Biz::Emboss::get_selected_text_volume(m_project_interactor);
        if (selected_text.volume && selected_text.volume->text_configuration) {
            const auto& prop = selected_text.volume->text_configuration->style.prop;
            project_context.current_bend_horizontal = static_cast<float>(Biz::Emboss::TextBender::clamp_angle(prop.bend_horizontal.value_or(0.0f)));
            project_context.current_bend_vertical = static_cast<float>(Biz::Emboss::TextBender::clamp_angle(prop.bend_vertical.value_or(0.0f)));
            project_context.current_bend_arc = static_cast<float>(Biz::Emboss::TextBender::clamp_angle(prop.bend_arc.value_or(0.0f)));

            const double L = HANDLE_STEM_LENGTH;
            const double x0 = 0.5 * L;
            const double x_min = 0.15 * L;
            const double x_max = 0.85 * L;

            if (project_context.bend_slider_y != nullptr) {
                double ty = std::clamp(static_cast<double>(project_context.current_bend_horizontal) / std::numbers::pi, -1.0, 1.0);
                double local_y_pos = x0 + ty * (x_max - x0);
                Transform3d xform = Transform3d::Identity();
                xform.translate(Vec3d(local_y_pos, 0.0, 0.0)).scale(0.5 * HANDLE_CUBE_SIZE);
                project_context.bend_slider_y->set_local_transform(xform);
            }

            if (project_context.bend_slider_x != nullptr) {
                double tx = std::clamp(static_cast<double>(project_context.current_bend_vertical) / std::numbers::pi, -1.0, 1.0);
                double local_x_pos = x0 + tx * (x_max - x0);
                Transform3d xform = Transform3d::Identity();
                xform.translate(Vec3d(local_x_pos, 0.0, 0.0)).scale(0.5 * HANDLE_CUBE_SIZE);
                project_context.bend_slider_x->set_local_transform(xform);
            }
            if (project_context.bend_slider_z != nullptr) {
                const double tz = static_cast<double>(project_context.current_bend_arc) / std::numbers::pi;
                Transform3d xform = Transform3d::Identity();
                xform.translate(Vec3d(x0 + tz * (x_max - x0), 0.0, 0.0)).scale(0.5 * HANDLE_CUBE_SIZE);
                project_context.bend_slider_z->set_local_transform(xform);
            }
        }
    }
}

std::optional<double> RotationGizmo::bend_mouse_position(const Scene::GizmoEventContext& ctx)
{
    const auto& project_context = m_projects.selected();
    Transform3d orientation = Transform3d::Identity();
    orientation.rotate(project_context.start_obb.rotation);
    orientation = orientation * axis_transform(project_context.bend_slider_axis);
    const Scene::Ray stem{project_context.start_obb.center, orientation.linear() * Vec3d::UnitX()};
    const auto& ray = ctx.pick_ray();
    if (std::abs(stem.direction.normalized().dot(ray.direction.normalized())) > 1.0 - 1e-8)
        return std::nullopt;
    double position = 0.0;
    if (!stem.closest_point_from_ray(ray, position))
        return std::nullopt;
    if (m_scene_presenter.selection_root().transform_modifier() != nullptr) {
        const auto& camera = m_scene_presenter.scene().camera();
        const double scale = camera.cam_projection().constant_screen_space_size_scale(
            camera, (project_context.start_obb.center - camera.position()).norm())
            * Scene::SELECTION_ROOT_SCALE_MODIFIER;
        if (scale <= 0.0)
            return std::nullopt;
        position /= scale;
    }
    return std::isfinite(position) ? std::optional<double>(position) : std::nullopt;
}

void RotationGizmo::finish_bend_drag()
{
    auto& project_context = m_projects.selected();
    if (!project_context.dragging_bend_slider)
        return;
    project_context.dragging_bend_slider = false;
    project_context.base_unbent_mesh = {};
    project_context.bend_slider_axis = AxisType::None;
    if (project_context.activated && project_context.main_node) {
        if (const auto bounds = m_scene_interactor.selection_bounding_box()) {
            Transform3d transform = Transform3d::Identity();
            transform.translate(bounds->oriented_bounding_box().center);
            transform.rotate(bounds->oriented_bounding_box().rotation);
            m_scene_presenter.selection_root().set_world_transform(transform);
        }
        update_bend_sliders_visibility_and_position();
    }
    if (project_context.bend_mesh_changed) {
        project_context.bend_mesh_changed = false;
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Rotate);
    }
}

void RotationGizmo::apply_bend_slider_drag(double local_x)
{
    auto& project_context = m_projects.selected();
    if (project_context.base_unbent_mesh.vertices.empty() || !std::isfinite(local_x))
        return;
    auto* volume = m_project_interactor.selected_project().find_volume_by_id(
        project_context.bend_target_element.object_id, project_context.bend_target_element.volume_id);
    if (!volume || !volume->text_configuration)
        return;

    const double position = std::clamp(local_x, 0.15 * HANDLE_STEM_LENGTH, 0.85 * HANDLE_STEM_LENGTH);
    const float angle = static_cast<float>(Biz::Emboss::TextBender::clamp_angle(
        (position / HANDLE_STEM_LENGTH - 0.5) * std::numbers::pi / 0.35));
    Biz::Emboss::BendParams params{project_context.current_bend_horizontal, project_context.current_bend_vertical,
        project_context.current_bend_arc};
    if (project_context.bend_slider_axis == AxisType::YAxis)
        params.horizontal_bend = angle;
    else if (project_context.bend_slider_axis == AxisType::XAxis)
        params.vertical_curl = angle;
    else if (project_context.bend_slider_axis == AxisType::ZAxis)
        params.vertical_arc = angle;
    if (params.horizontal_bend == project_context.current_bend_horizontal
        && params.vertical_curl == project_context.current_bend_vertical
        && params.vertical_arc == project_context.current_bend_arc)
        return;

    auto mesh = project_context.base_unbent_mesh;
    Biz::Emboss::TextBender::bend_mesh(mesh, params, project_context.base_mesh_bbox);
    auto bent_mesh = Biz::Algorithms::TriangleMesh::construct(std::move(mesh));
    project_context.current_bend_horizontal = params.horizontal_bend;
    project_context.current_bend_vertical = params.vertical_curl;
    project_context.current_bend_arc = params.vertical_arc;
    // Publish matching parameters before synchronous mesh/selection notifications.
    // Finishing or interrupting the drag then records one undo step.
    volume->text_configuration->style.prop.bend_horizontal = params.horizontal_bend;
    volume->text_configuration->style.prop.bend_vertical = params.vertical_curl;
    volume->text_configuration->style.prop.bend_arc = params.vertical_arc;
    volume->text_configuration->bend_reference = project_context.base_mesh_bbox;
    Biz::Scene::SceneInteractor::VolumeMeshReplacements replacements;
    replacements.push_back({project_context.bend_target_element, std::move(bent_mesh)});
    const auto updated = m_scene_interactor.change_volume_meshes(std::move(replacements));
    if (!updated.empty()) {
        project_context.bend_target_element = updated.front();
        project_context.bend_mesh_changed = true;
    }

    Scene::Node* slider = project_context.bend_slider_axis == AxisType::YAxis
        ? project_context.bend_slider_y : project_context.bend_slider_axis == AxisType::XAxis
        ? project_context.bend_slider_x : project_context.bend_slider_z;
    if (slider) {
        Transform3d transform = Transform3d::Identity();
        transform.translate(Vec3d(position, 0.0, 0.0)).scale(0.5 * HANDLE_CUBE_SIZE);
        slider->set_local_transform(transform);
    }
    if (m_window)
        m_window->set_bend_values(rad2deg(static_cast<double>(params.horizontal_bend)),
            rad2deg(static_cast<double>(params.vertical_curl)), rad2deg(static_cast<double>(params.vertical_arc)));
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
