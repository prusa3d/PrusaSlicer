#include "Slic3r/App/Scene/AbstractCameraGizmo.hpp"
#include "Slic3r/App/Scene/Plane.hpp"
#include "Slic3r/App/Scene/CameraHelper.hpp"
#include "Slic3r/App/Platform/CommandName.hpp"
#include "Slic3r/App/UIItemCommand.hpp"
#include "Slic3r/App/Scene/ClickDetector.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/App/Scene/MouseBindingScheme.hpp"
#include "Slic3r/App/AppServices.hpp"

#include <tracy/Tracy.hpp>

using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

using namespace Slic3r::Biz;

namespace Slic3r::App::Scene {

using CommandName = Platform::CommandName;
using FuncCommandExtraOpts = Platform::FuncCommandExtraOpts;

void AbstractCameraGizmo::register_commands(Platform::CommandRegistry& registry)
{
    registry
        .register_command(
            std::make_unique<UIItemCommand>(
                CommandName::ZoomIn,
                [this]() { update_zoom(1.); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::I}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<UIItemCommand>(
                CommandName::ZoomOut,
                [this]() { update_zoom(-1.); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::O}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<UIItemCommand>(
                CommandName::CameraProjectionSwitch,
                [this]() { m_scene_provider.scene().camera_trackball().switch_projection_type(); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::K}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<UIItemCommand>(
                CommandName::LookAtActiveBed,
                [this]() { center_camera_on_selected_bed(true); },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::B}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<UIItemCommand>(
                CommandName::CameraDefaultView,
                [this]()
                {
                    look_at(
                        m_scene_provider.scene().camera_trackball().target(),
                        DEFAULT_AZIMUTH,
                        DEFAULT_ZENITH
                    );
                },
                UIItemCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Num0},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Kp0}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::CameraTopView,
                [this]()
                { look_at(m_scene_provider.scene().camera_trackball().target(), M_PI_2, M_PI); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Num1},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Kp1}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::CameraBottomView,
                [this]()
                { look_at(m_scene_provider.scene().camera_trackball().target(), M_PI_2, 0.0); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Num2},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Kp2}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::CameraFrontView,
                [this]()
                { look_at(m_scene_provider.scene().camera_trackball().target(), M_PI_2, M_PI_2); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Num3},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Kp3}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::CameraRearView,
                [this]()
                { look_at(m_scene_provider.scene().camera_trackball().target(), -M_PI_2, M_PI_2); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Num4},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Kp4}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::CameraLeftView,
                [this]()
                { look_at(m_scene_provider.scene().camera_trackball().target(), 0.0, M_PI_2); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts =
                        Platform::KeyboardShortcuts{
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Num5},
                            Platform::KeyboardShortcut{0, Platform::KeyCode::Kp5}
                        }
                }
            )
        )
        .register_command(
            std::make_unique<Platform::FuncCommand>(
                CommandName::CameraRightView,
                [this]()
                { look_at(m_scene_provider.scene().camera_trackball().target(), M_PI, M_PI_2); },
                FuncCommandExtraOpts{
                    .keyboard_shortcuts = Platform::KeyboardShortcuts{
                        Platform::KeyboardShortcut{0, Platform::KeyCode::Num6},
                        Platform::KeyboardShortcut{0, Platform::KeyCode::Kp6}
                    }
                }
            )
        );
}

// TODO: move these draw_* function into own module so they can be reused (+ add drawing-in-plane renderer utilizing x-axis and y-axis and origin on the plane)
template <typename V>
void draw_circle(Render::DynamicGeometry<V>& g, const Vec3f& position, const Vec3f& x_axis, const Vec3f& y_axis, float radius, size_t resolution = 32)
{
    auto builder = g.build_primitive(Render::PrimitiveType::LineLoop);
    for (size_t i = 0; i < resolution; i++) {
        auto phi = M_PI * 2 * i / resolution;
        Vec3f pt = x_axis * radius * std::cos(phi) + y_axis * radius * std::sin(phi) + position;
        builder.vertex(pt);
    }
}

template <typename V>
void draw_square(Render::DynamicGeometry<V>& g, const Vec3f& position, const Vec3f& x_axis, const Vec3f& y_axis, float radius)
{
    auto builder = g.build_primitive(Render::PrimitiveType::LineLoop);
    for (size_t i = 0; i < 4; i++) {
        const float sx = i / 2 == 0 ? -1 : 1;
        const float sy = i == 0 || i == 3 ? -1 : 1;
        Vec3f pt       = x_axis * radius * sx + y_axis * radius * sy + position;
        builder.vertex(pt);
    }
}

template <typename V>
void draw_cross(Render::DynamicGeometry<V>& g, const Vec3f& position, const Vec3f& x_axis, const Vec3f& y_axis, float radius)
{
    auto builder = g.build_primitive(Render::PrimitiveType::Lines);
    for (size_t i = 0; i < 4; i++) {
        const float sx = i % 2 == 0 ? -1 : 1;
        const float sy = i == 0 || i == 3 ? -1 : 1;
        Vec3f pt = x_axis * radius * sx + y_axis * radius * sy + position;
        builder.vertex(pt);
    }
}

bool AbstractCameraGizmo::pick_plane(double mouse_x, double mouse_y, const Render::ScreenInfo& screen_info, Vec3d& out_plane_point)
{
    auto& scene = m_scene_provider.scene();

    auto& cam = scene.camera();
    auto r = cam.ray_at(screen_info.mouse_to_screen(mouse_x), screen_info.mouse_to_screen(mouse_y));

    Vec3d n  = cam.forward();
    Vec3d p  = cam.position();
    double q = p.dot(n) / n.dot(n);
    const Plane plane{n, -scene.camera_trackball().distance_to_target() - q};

    double t;
    // r.origin = Vec3d::Zero();
    if (plane.intersects(r, t)) {
        out_plane_point = r.point_at(t);
#if CAMERA_GIZMO_DEBUG
        Vec3d u, v;
        plane.vectors_in_plane(u, v);
        const size_t N{32};
        const double R{30};
        draw_circle(m_dynamic_geometry, out_plane_point.cast<float>(), u.cast<float>(), v.cast<float>(), R, N);
#endif
        return true;
    }
    return false;
}

void AbstractCameraGizmo::center_camera_on_selected_bed(bool animated)
{
    Domain::SelectionId selected_project_id = m_project_interactor.selected_project_id();
    Domain::BedRef selected_bed = m_project_interactor.scene_interactor().bed_selection().last_selected_bed();
    if (animated)
        animated_center_camera_on_bed(m_workbench.project(selected_project_id), selected_bed,
            m_scene_provider.scene().camera_trackball(), m_animation_manager);
    else
        center_camera_on_bed(m_workbench.project(selected_project_id), selected_bed, m_scene_provider.scene().camera_trackball());
}

GizmoActivationState AbstractCameraGizmo::on_mouse(GizmoEventContext& ctx, bool only_active)
{
    ZoneScoped;

#if CAMERA_GIZMO_DEBUG
    m_dynamic_geometry.clear();
    {
        const auto& scene         = m_scene_provider.scene();
        const auto& cam           = scene.camera();
        const auto& cam_trackball = scene.camera_trackball();

        draw_square(
            m_dynamic_geometry,
            cam_trackball.cam_focal().cast<float>(),
            cam.up().cast<float>(),
            cam.right().cast<float>(),
            100
        );
        draw_cross(
            m_dynamic_geometry,
            cam_trackball.cam_focal().cast<float>(),
            cam.up().cast<float>(),
            cam.right().cast<float>(),
            100
        );
    }
#endif

    const Platform::MouseEvent& event = ctx.mouse_event();
    const auto type                   = event.type();
    const bool ignore_dragging =
        event.key_modifiers() == static_cast<Platform::KeyModifiers>(Platform::KeyModifier::Ctrl);
    if (type == Platform::MouseEvent::Type::ButtonDown) {
        const MouseBindingScheme scheme = current_mouse_binding_scheme();
        bool pan    = scheme.is_pan_start(event.button(), event.key_modifiers());
        bool rotate = scheme.is_rotate_start(event.button(), event.key_modifiers());
        if (!pan && !rotate)
            return GizmoActivationState::Inactive;
        // Object-drag (MouseDragDetector) and box-select (QuickSelectGizmo) only ever claim Left;
        // yield to them regardless of which scheme currently routes Left to orbit/pan/neither.
        if (event.button() == Platform::MouseButton::Left) {
            if (!pan && (!ignore_dragging && any_draggable(ctx)))
                return GizmoActivationState::Inactive;
            if (event.key_modifiers() != 0 && !ignore_dragging)
                return GizmoActivationState::Inactive;
        }
        m_state  = pan ? State::Panning : State::Rotating;
        m_last_x = event.x();
        m_last_y = event.y();
    } else if (type == Platform::MouseEvent::Type::Move) {
        if (m_state == State::Inactive)
            return GizmoActivationState::Inactive;

        float dx      = event.x() - m_last_x;
        float dy      = event.y() - m_last_y;
        float delta_x = dx / ctx.screen_info().logical_width();
        float delta_y = dy / ctx.screen_info().logical_height();

        // limit the activation after reaching 4px+ distance
        if (!m_was_activated && dx * dx + dy * dy <= ClickDetector::MAX_ALLOWED_DISTANCE_SQ_PX) {
            return GizmoActivationState::Probing;
        } else {
            m_was_activated = true;
        }

        if (m_state == State::Rotating)
            update_rotation(delta_x, delta_y, 0.25);
        else if (m_state == State::Panning) {
            Vec3d current_mouse_world_pos;
            Vec3d last_mouse_world_pos;
            if (pick_plane(event.x(), event.y(), ctx.screen_info(), current_mouse_world_pos)
                && pick_plane(m_last_x, m_last_y, ctx.screen_info(), last_mouse_world_pos))
            {
                bool shift_down = (ctx.mouse_event().key_modifiers() & Platform::KeyModifiers(Platform::KeyModifier::Shift)) != 0;
                update_pan(last_mouse_world_pos - current_mouse_world_pos, shift_down);
            } else
                return GizmoActivationState::Inactive;
        }

        m_last_x = event.x();
        m_last_y = event.y();

        return GizmoActivationState::Active;
    } else if (type == Platform::MouseEvent::Type::ButtonUp) {
        m_state = State::Inactive;
        return m_was_activated ? GizmoActivationState::Done : GizmoActivationState::Inactive;
    } else if (type == Platform::MouseEvent::Type::Wheel) {
        float wheel_delta_y = event.wheel_delta_y();
        if (AppServices::instance().app_config().get<bool>("reverse_mouse_wheel_zoom"))
            wheel_delta_y = -wheel_delta_y;
        update_zoom(wheel_delta_y);
        return (m_state == State::Inactive) ? GizmoActivationState::Inactive : GizmoActivationState::Done;
    }
    if (m_state == State::Inactive)
        return GizmoActivationState::Inactive;
    return only_active ? GizmoActivationState::Active : GizmoActivationState::Probing;
}

void AbstractCameraGizmo::on_cycle_prepare()
{
    m_state = State::Inactive;
    m_was_activated = false;
}

#if CAMERA_GIZMO_DEBUG
void AbstractCameraGizmo::render_scene(Render::CommandBuffer& cmd_buffer)
{
    if (!m_dynamic_geometry.empty()) {
        cmd_buffer.set_depth_test_enabled(true);
        m_dynamic_geometry.draw(cmd_buffer, Render::Material{}.set_shader(Render::Context::instance().shader_manager().shader("flat")));
    }
}
#endif


void AbstractCameraGizmo::update_pan(const Vec3d& delta, bool synchronize_cam_pivot)
{
    auto& scene = m_scene_provider.scene();
    // auto& cam = scene.camera();
    // const auto& model = cam.model();
    // auto right = model.block<3, 1>(0, 0);
    // auto up = model.block<3, 1>(0, 1);
    auto& trackball = scene.camera_trackball();

    // double dist = trackball.cam_focal_dist();
    // trackball.set_focal_point(trackball.cam_focal() + right * -delta_x * dist + up * delta_y * dist);
    trackball.set_target(trackball.target() + delta);
    if (synchronize_cam_pivot)
        trackball.synchronize_pivot_with_target();
}

void AbstractCameraGizmo::update_zoom(float wheel_delta_y)
{
    // On OSX with TrackPad when doing a small movement with two fingers (the scroll gesture)
    // the wheel_delta_y may be 0 (!) so prevent handling such events (this would lead to NaN in
    // zoom factor)
    if (wheel_delta_y != 0)
        m_scene_provider.scene().camera_trackball().update_zoom(wheel_delta_y / std::abs(wheel_delta_y));
}

void AbstractCameraGizmo::update_rotation(float delta_x, float delta_y, float delta_for_180_rotation)
{
    auto& scene     = m_scene_provider.scene();
    auto& trackball = scene.camera_trackball();

    const double delta_to_angle_factor{1.0 / delta_for_180_rotation * M_PI};
    trackball.add_azimuth_and_zenith(delta_x * delta_to_angle_factor, delta_y * delta_to_angle_factor, true);
}

void AbstractCameraGizmo::look_at(const Vec3d& pos, double azimuth, double zenith)
{
    auto& scene     = m_scene_provider.scene();
    auto& trackball = scene.camera_trackball();
    trackball.set_target(pos);
    trackball.set_azimuth_and_zenith(azimuth, zenith);
}

} // namespace Slic3r::App::Scene
