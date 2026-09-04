#include "Slic3r/App/Plater/ScaleGizmo.hpp"
#include <fmt/ostream.h>
#include "Slic3r/App/Plater/GizmoNodeTag.hpp"
#include "Slic3r/App/Plater/PlaterSceneLayer.hpp"
#include "Slic3r/App/Scene/GeometryDataFactory.hpp"
#include "Slic3r/App/Plater/PlaterGizmosHelper.hpp"

namespace Slic3r::App::Plater {

ScaleGizmo::ScaleGizmo(
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
    m_projects(project_interactor)
{
    m_scene_interactor.add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_scene_provider.add_listener<App::Plater::ISelectionExtentsChangedListener>(this);
}

ScaleGizmo::~ScaleGizmo()
{
    m_scene_interactor.remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
    m_scene_provider.remove_listener<App::Plater::ISelectionExtentsChangedListener>(this);
}

void ScaleGizmo::on_cycle_prepare() {}

static Domain::Vec3d get_drag_cube_direction(DragCube drag_cube)
{
    switch (drag_cube) {
    case DragCube::Left:
        return Domain::Vec3d{-1, 0, 0};
    case DragCube::Right:
        return Domain::Vec3d{1, 0, 0};
    case DragCube::Front:
        return Domain::Vec3d{0, -1, 0};
    case DragCube::Back:
        return Domain::Vec3d{0, 1, 0};
    case DragCube::Top:
        return Domain::Vec3d{0, 0, 1};
    case DragCube::Bottom:
        return Domain::Vec3d{0, 0, -1};
    case DragCube::FrontLeft:
        return Domain::Vec3d{-1, -1, 0}.normalized();
    case DragCube::FrontRight:
        return Domain::Vec3d{1, -1, 0}.normalized();
    case DragCube::BackLeft:
        return Domain::Vec3d{-1, 1, 0}.normalized();
    case DragCube::BackRight:
        return Domain::Vec3d{1, 1, 0}.normalized();
    default:
        PANIC("Unknown drag cube");
    }
}

Scene::GizmoActivationState ScaleGizmo::on_mouse(Scene::GizmoEventContext& ctx, bool only_active)
{
    ProjectContext& project_context{m_projects.selected()};
    const auto event_type = ctx.mouse_event().type();
    if (event_type != Platform::MouseEvent::Type::ButtonDown
        && event_type != Platform::MouseEvent::Type::Move
        && event_type != Platform::MouseEvent::Type::ButtonUp)
    {
        project_context.dragging = false;
        return Scene::GizmoActivationState::Inactive;
    }

    const auto& pick_ray = ctx.pick_ray();

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        const Scene::Node* node = ctx.pick_result_node_with_tag_of_type<ScaleGizmoNodeTag>();
        if (node == nullptr) {
            project_context.dragging = false;
            return Scene::GizmoActivationState::Inactive;
        }
        const Domain::Transform3d transform{
            m_scene_provider.plain_selection_root().world_transform()
        };

        const ScaleGizmoNodeTag& tag{*node->tag_of_type<ScaleGizmoNodeTag>()};
        if (tag.drag_cube == DragCube::None) {
            project_context.dragging = false;
            return Scene::GizmoActivationState::Inactive;
        }

        project_context.scale_ray.origin    = transform.translation();
        project_context.scale_ray.direction = transform.rotation() * get_drag_cube_direction(tag.drag_cube);
        project_context.scale_axis          = tag.axis;
    }

    double t;
    if (!project_context.scale_ray.closest_point_from_ray(pick_ray, t)) {
        project_context.dragging = false;
        return Scene::GizmoActivationState::Inactive;
    }

    const std::optional<Biz::Scene::SelectionExtents> selection_bounding_box{
        m_scene_interactor.selection_bounding_box()
    };
    if (!selection_bounding_box) {
        return Scene::GizmoActivationState::Inactive;
    }

    if (event_type == Platform::MouseEvent::Type::ButtonDown) {
        project_context.start_t   = t;
        project_context.start_obb = selection_bounding_box->oriented_bounding_box();
        project_context.was_floating = selection_bounding_box->is_floating();
        project_context.dragging  = true;
        return Scene::GizmoActivationState::Active;
    }

    if (!project_context.dragging)
        return Scene::GizmoActivationState::Inactive;

    const std::optional<int> axis{get_axis_index(project_context.scale_axis)};
    const double delta{t - project_context.start_t};
    const double min_abs_scale{1};

    const Domain::Vec3d initial_scale{project_context.start_obb.dimensions};

    Domain::Vec3d scale_by{Domain::Vec3d::Ones()};
    if (axis) {
        const double min_scale_factor{min_abs_scale / initial_scale(*axis)};
        double scale_factor{(initial_scale(*axis) + 2.0 * delta) / initial_scale(*axis)};
        scale_by(*axis) = std::max(scale_factor, min_scale_factor);
    } else {
        const double min_scale_factor{min_abs_scale / initial_scale.minCoeff()};
        const double scale_factor{
            (initial_scale.head<2>().norm() + 2 * delta) / initial_scale.head<2>().norm()
        };
        scale_by = std::max(scale_factor, min_scale_factor) * Domain::Vec3d::Ones();
    }

    const Domain::SquareMatrix4d scale_matrix{get_scale_matrix(
        project_context.start_obb.rotation,
        project_context.start_obb.center,
        scale_by
    )};

    project_context.xform_memento.forced_volume_mode = true;
    m_scene_interactor.transform_selection(scale_matrix, project_context.xform_memento, !project_context.was_floating);

    if (event_type == Platform::MouseEvent::Type::ButtonUp) {
        m_scene_interactor.finalize_transform_selection(
            project_context.xform_memento,
            false
        );
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::Scale);
        project_context.dragging = false;
        return Scene::GizmoActivationState::Done;
    }

    return Scene::GizmoActivationState::Active;
}

void ScaleGizmo::on_transient_mouse(Scene::GizmoEventContext& ctx) {}

void ScaleGizmo::on_activated()
{
    m_projects.selected().activated = true;
    m_window->on_activated(m_project_interactor.selected_project_id());
    update_handle_nodes();
}

void ScaleGizmo::on_deactivated()
{
    m_projects.selected().activated = false;
    m_window->on_deactivated();
    m_scene_provider.clear_selection_root_children();
}

bool ScaleGizmo::enabled() const
{
    const Biz::Scene::ObjectSelection& selection{m_scene_interactor.object_selection()};
    return !selection.empty() && !selection.contains_wipe_tower();
}

std::unique_ptr<GizmoWindow> ScaleGizmo::release_ui_window()
{
    auto window{std::make_unique<ScaleDialog>(m_project_interactor)};
    m_window = window.get();
    return window;
}

struct BuilderContext
{
    Scene::NodeBuilder& builder;
    Render::Device& device;
    Scene::GeometryDataFactory& data_factory;
};

struct AxisLineParams
{
    const std::string debug_name;
    const Render::Material material;
    const Domain::Vec3d position;
    const Domain::SquareMatrix3d rotation;
    double length;
    ScaleGizmoNodeTag tag;
};

static void build_axis_line(const BuilderContext& builder_context, const AxisLineParams& params)
{
    builder_context.builder.child(
        [&](Scene::NodeBuilder& builder)
        {
            builder_context.builder.set_debug_name(params.debug_name)
                .set_tag(params.tag)
                .set_mesh(
                    builder_context.data_factory.geometry(Scene::GeometryDataId::Segment),
                    params.material,
                    Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                )
                .set_material_override(params.material);

            Domain::Transform3d transform{Domain::Transform3d::Identity()};
            transform.translate(params.position);
            transform.rotate(params.rotation);
            transform.scale(Domain::Vec3d{params.length, 0, 0});
            builder_context.builder.set_transform(transform);
        }
    );
}

struct DragCubeParams
{
    const std::string debug_name;
    const Render::Material material;
    const Domain::Vec3d position;
    ScaleGizmoNodeTag tag;
};

static void build_drag_cube(const BuilderContext& builder_context, const DragCubeParams& params)
{
    const double scale{3};

    builder_context.builder.child(
        [&](Scene::NodeBuilder& builder)
        {
            auto geom = builder_context.data_factory.geometry(Scene::GeometryDataId::Cube);
            auto mesh = builder_context.data_factory.triangle_mesh(Scene::GeometryDataId::Cube);

            builder_context.builder.set_debug_name(params.debug_name)
                .set_tag(params.tag)
                .set_material_override(params.material)
                .set_mesh(
                    geom,
                    params.material,
                    Scene::RenderLayerId(PlaterSceneLayer::GizmoHandles)
                )
                .set_aabb(mesh->aabb_mesh());

            Domain::Transform3d transform{Domain::Transform3d::Identity()};
            transform.translate(params.position);
            transform.scale(scale);
            builder.set_transform(transform);
        }
    );
}

static void
build_square_handle(const BuilderContext& builder_context, const Domain::Vec2d& size, double gap)
{
    const Render::Material cube_material{
        Render::Material{}
            .set_shader(builder_context.device.context().shader_manager().shader("gouraud_light"))
            .set_uniform("uniform_color", Domain::ColorRGBA::ORANGE())
    };
    const Render::Material line_material{
        Render::Material{}
            .set_shader(builder_context.device.context().shader_manager().shader("flat"))
            .set_uniform("uniform_color", Domain::ColorRGBA::GRAY())
    };

    const Domain::Vec2d offset{size.x() / 2.0 + gap, size.y() / 2.0 + gap};
    const Domain::Vec2d line_length{size + 2 * gap * Domain::Vec2d::Ones()};

    builder_context.builder.child(
        [&](Scene::NodeBuilder& builder)
        {
            builder.set_debug_name("square_handle").set_tag(ScaleGizmoNodeTag{AxisType::XYAxis});
            build_drag_cube(
                builder_context,
                DragCubeParams{
                    .debug_name = "front_left_cube",
                    .material   = cube_material,
                    .position   = Domain::Vec3d{-offset.x(), -offset.y(), 0},
                    .tag        = ScaleGizmoNodeTag{AxisType::XYAxis, DragCube::FrontLeft}
                }
            );
            build_drag_cube(
                builder_context,
                DragCubeParams{
                    .debug_name = "front_right_cube",
                    .material   = cube_material,
                    .position   = Domain::Vec3d{offset.x(), -offset.y(), 0},
                    .tag        = ScaleGizmoNodeTag{AxisType::XYAxis, DragCube::FrontRight}
                }
            );
            build_drag_cube(
                builder_context,
                DragCubeParams{
                    .debug_name = "back_left_cube",
                    .material   = cube_material,
                    .position   = Domain::Vec3d{-offset.x(), offset.y(), 0},
                    .tag        = ScaleGizmoNodeTag{AxisType::XYAxis, DragCube::BackLeft}
                }
            );
            build_drag_cube(
                builder_context,
                DragCubeParams{
                    .debug_name = "back_right_cube",
                    .material   = cube_material,
                    .position   = Domain::Vec3d{offset.x(), offset.y(), 0},
                    .tag        = ScaleGizmoNodeTag{AxisType::XYAxis, DragCube::BackRight}
                }
            );
            build_axis_line(
                builder_context,
                AxisLineParams{
                    .debug_name = "front_line",
                    .material   = line_material,
                    .position   = Domain::Vec3d{-offset.x(), -offset.y(), 0},
                    .rotation   = Domain::SquareMatrix3d::Identity(),
                    .length     = line_length.x(),
                    .tag        = ScaleGizmoNodeTag{AxisType::XYAxis}
                }
            );
            build_axis_line(
                builder_context,
                AxisLineParams{
                    .debug_name = "back_line",
                    .material   = line_material,
                    .position   = Domain::Vec3d{-offset.x(), offset.y(), 0},
                    .rotation   = Domain::SquareMatrix3d::Identity(),
                    .length     = line_length.x(),
                    .tag        = ScaleGizmoNodeTag{AxisType::XYAxis}
                }
            );
            build_axis_line(
                builder_context,
                AxisLineParams{
                    .debug_name = "left_line",
                    .material   = line_material,
                    .position   = Domain::Vec3d{-offset.x(), -offset.y(), 0},
                    .rotation =
                        Eigen::AngleAxisd(std::numbers::pi / 2.0, Domain::Vec3d::UnitZ()).matrix(),
                    .length = line_length.y(),
                    .tag    = ScaleGizmoNodeTag{AxisType::XYAxis}
                }
            );
            build_axis_line(
                builder_context,
                AxisLineParams{
                    .debug_name = "right_line",
                    .material   = line_material,
                    .position   = Domain::Vec3d{offset.x(), -offset.y(), 0},
                    .rotation =
                        Eigen::AngleAxisd(std::numbers::pi / 2.0, Domain::Vec3d::UnitZ()).matrix(),
                    .length = line_length.y(),
                    .tag    = ScaleGizmoNodeTag{AxisType::XYAxis}
                }
            );
        }
    );
}

struct AxisHandleParams
{
    const std::string& debug_name;
    AxisType axis;
    double width;
    double gap;
    Domain::Transform3d transform;
};

static void build_axis_handle(const BuilderContext& builder_context, const AxisHandleParams& params)
{
    const Render::Material cube_material{
        Render::Material{}
            .set_shader(builder_context.device.context().shader_manager().shader("gouraud_light"))
            .set_uniform("uniform_color", axis_color(params.axis))
    };
    const Render::Material line_material{
        Render::Material{}
            .set_shader(builder_context.device.context().shader_manager().shader("flat"))
            .set_uniform("uniform_color", axis_color(params.axis))
    };

    std::string first_cube_debug_name{"left_drag_cube"};
    std::string second_cube_debug_name{"right_drag_cube"};
    DragCube first_cube_type{DragCube::Left};
    DragCube second_cube_type{DragCube::Right};

    if (params.axis == AxisType::YAxis) {
        first_cube_debug_name  = "front_drag_cube";
        second_cube_debug_name = "back_drag_cube";
        first_cube_type        = DragCube::Front;
        second_cube_type       = DragCube::Back;
    } else if (params.axis == AxisType::ZAxis) {
        first_cube_debug_name  = "bottom_drag_cube";
        second_cube_debug_name = "top_drag_cube";
        first_cube_type        = DragCube::Bottom;
        second_cube_type       = DragCube::Top;
    } else {
        ASSERT(params.axis == AxisType::XAxis);
    }

    builder_context.builder.child(
        [&](Scene::NodeBuilder& builder)
        {
            builder.set_debug_name(params.debug_name)
                .set_tag(ScaleGizmoNodeTag{params.axis})
                .set_transform(params.transform);
            build_drag_cube(
                builder_context,
                DragCubeParams{
                    .debug_name = first_cube_debug_name,
                    .material   = cube_material,
                    .position   = Domain::Vec3d{-params.width / 2 - params.gap, 0, 0},
                    .tag        = ScaleGizmoNodeTag{params.axis, first_cube_type}
                }
            );

            double axis_line_width{params.width + 2 * params.gap};
            build_axis_line(
                builder_context,
                AxisLineParams{
                    .debug_name = "line",
                    .material   = line_material,
                    .position   = Domain::Vec3d{-axis_line_width / 2, 0, 0},
                    .rotation   = Domain::SquareMatrix3d::Identity(),
                    .length     = axis_line_width,
                    .tag        = ScaleGizmoNodeTag{params.axis}
                }
            );
            build_drag_cube(
                builder_context,
                DragCubeParams{
                    .debug_name = second_cube_debug_name,
                    .material   = cube_material,
                    .position   = Domain::Vec3d{params.width / 2 + params.gap, 0, 0},
                    .tag        = ScaleGizmoNodeTag{params.axis, second_cube_type}
                }
            );
        }
    );
}

std::unique_ptr<Scene::Node> ScaleGizmo::generate_handle_nodes() const
{
    using Biz::Scene::SelectionState;

    auto& scene = m_scene_provider.scene();

    Scene::NodeBuilder builder{scene};
    builder.set_debug_name("scale_handles");
    builder.set_tag(ScaleGizmoNodeTag{});

    const auto selection_bounding_box{m_scene_interactor.selection_bounding_box()};
    if (!selection_bounding_box) {
        return nullptr;
    }
    const Biz::Scene::OrientedBoundingBox& bounding_box{selection_bounding_box->oriented_bounding_box()};

    const BuilderContext builder_context{builder, m_device, m_data_factory};

    const double gap{4};

    build_axis_handle(
        builder_context,
        AxisHandleParams{
            .debug_name = "x_axis_handle",
            .axis       = AxisType::XAxis,
            .width      = bounding_box.dimensions.x(),
            .gap        = gap,
            .transform  = Domain::Transform3d{Domain::SquareMatrix4d::Identity()}
        }
    );

    Domain::Transform3d y_transform{Domain::Transform3d::Identity()};
    y_transform.rotate(Eigen::AngleAxisd(std::numbers::pi / 2.0, Domain::Vec3d::UnitZ()));
    build_axis_handle(
        builder_context,
        AxisHandleParams{
            .debug_name = "y_axis_handle",
            .axis       = AxisType::YAxis,
            .width      = bounding_box.dimensions.y(),
            .gap        = gap,
            .transform  = y_transform
        }
    );

    Domain::Transform3d z_transform{Domain::Transform3d::Identity()};
    z_transform.rotate(Eigen::AngleAxisd(-std::numbers::pi / 2.0, Domain::Vec3d::UnitY()));
    build_axis_handle(
        builder_context,
        AxisHandleParams{
            .debug_name = "z_axis_handle",
            .axis       = AxisType::ZAxis,
            .width      = bounding_box.dimensions.z(),
            .gap        = gap,
            .transform  = z_transform
        }
    );

    build_square_handle(builder_context, bounding_box.dimensions.head<2>(), gap);

    return builder.build();
}

void ScaleGizmo::update_handle_nodes()
{
    ProjectContext& project_context{m_projects.selected()};
    if (!project_context.activated) {
        return;
    }
    m_scene_provider.clear_selection_root_children();

    Scene::Scene& scene{m_scene_provider.scene()};

    std::unique_ptr<Scene::Node> handle_nodes{generate_handle_nodes()};
    if (!handle_nodes) {
        return;
    }
    project_context.handle_nodes = handle_nodes.get();
    scene.add_child(handle_nodes.release(), &m_scene_provider.plain_selection_root());
}

void ScaleGizmo::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection&
)
{
    if (!enabled()) {
        return;
    }
    update_handle_nodes();
}

void ScaleGizmo::on_scene_selection_transformed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection&
)
{
    update_handle_nodes();
}

void ScaleGizmo::on_scene_selection_bounding_box_changed(
    Domain::SelectionId project_id,
    const std::optional<Biz::Scene::SelectionExtents>&
)
{
    update_handle_nodes();
}

} // namespace Slic3r::App::Plater
