#include "Slic3r/App/Mouse3D/Mouse3DController.hpp"

#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/Scene/Camera.hpp"
#include "Slic3r/App/Scene/CameraProjectionParameters.hpp"
#include "Slic3r/App/Scene/CameraTrackballController.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Log.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>

using Slic3r::Domain::Vec3d;

namespace Slic3r::App::Mouse3D {

namespace {

constexpr size_t MaxQueuedMotions = 15;
// Normalizes the raw device axis values.
constexpr double AxisDivisor      = 350.0;
constexpr double TranslationScale = 2.5;
constexpr double ZoomScale        = 0.1;

double decode_hid_axis(std::uint8_t low_byte, std::uint8_t high_byte)
{
    return static_cast<std::int16_t>(low_byte | (high_byte << 8));
}

Vec3d decode_hid_axes(std::span<const std::uint8_t> report, size_t first_byte)
{
    return Vec3d(
        decode_hid_axis(report[first_byte], report[first_byte + 1]),
        decode_hid_axis(report[first_byte + 2], report[first_byte + 3]),
        decode_hid_axis(report[first_byte + 4], report[first_byte + 5])
    );
}

Vec3d apply_deadzone(const Vec3d& axes, double deadzone)
{
    return axes.unaryExpr(
        [deadzone](double value) { return std::abs(value) > deadzone ? value : 0.0; }
    );
}

double orthographic_equivalent_zoom(const Scene::Camera& camera)
{
    return camera.cam_projection().type() == Scene::CameraProjectionType::Perspective ?
        Scene::CameraProjectionParameters::orthographic_zoom_from_perspective(camera.zoom()) :
        camera.zoom();
}

} // namespace

Mouse3DController::Mouse3DController(
    AppConfig& app_config,
    AppConfigInteractor& app_config_interactor,
    Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher
) :
    m_app_config(app_config),
    m_main_thread_dispatcher(main_thread_dispatcher),
    m_app_config_changed_listener_scope(app_config_interactor, *this)
{
    load_settings();
}

void Mouse3DController::handle_hid_report(std::span<const std::uint8_t> report)
{
    if (report.size() != 7 && report.size() != 13) {
        return;
    }

    // Report 1 carries the translation (the 13 bytes long one also the rotation), report 2 the
    // rotation. Buttons (report 3) are left to the driver, battery (report 23) is not used.
    if (report[0] == 1) {
        const Vec3d rotation = report.size() == 13 ? decode_hid_axes(report, 7) : Vec3d::Zero();
        handle_axis_input(decode_hid_axes(report, 1), rotation);
    } else if (report[0] == 2) {
        handle_axis_input(Vec3d::Zero(), decode_hid_axes(report, 1));
    }
}

void Mouse3DController::handle_axis_input(const Vec3d& translation, const Vec3d& rotation)
{
    if (!m_application_active) {
        return;
    }

    std::scoped_lock lock(m_input_mutex);
    const Vec3d device_translation =
        apply_deadzone(translation / AxisDivisor, m_settings.translation_deadzone);
    if (!device_translation.isZero()) {
        const Vec3d translation_motion(
            -device_translation.x(),
            device_translation.y(),
            device_translation.z()
        );
        enqueue_motion({Motion::Type::Translation, translation_motion});
    }

    const Vec3d device_rotation =
        apply_deadzone(rotation / AxisDivisor, m_settings.rotation_deadzone);
    if (!device_rotation.isZero()) {
        enqueue_motion({Motion::Type::Rotation, device_rotation});
        if (device_rotation.x() != 0.0) {
            ++m_driver_mouse_wheel_counter;
        }
    }
}

void Mouse3DController::set_application_active(bool active)
{
    m_application_active = active;
}

bool Mouse3DController::consume_driver_mouse_wheel_event()
{
    std::scoped_lock lock(m_input_mutex);
    if (m_driver_mouse_wheel_counter == 0) {
        // No rotation around the X axis since the last wheel event, the event comes from a mouse.
        return false;
    }

    const bool rotation_queued = std::ranges::any_of(
        m_motion_queue,
        [](const Motion& motion) { return motion.type == Motion::Type::Rotation; }
    );
    if (rotation_queued) {
        --m_driver_mouse_wheel_counter;
    } else {
        // The rotations were already applied, this is the last wheel event attributed to them.
        m_driver_mouse_wheel_counter = 0;
    }

    return true;
}

void Mouse3DController::device_connected(const std::string& device_name)
{
    if (m_connected) {
        return;
    }

    m_connected = true;
    SPDLOG_INFO("3D mouse connected: {}", device_name);
}

void Mouse3DController::device_disconnected()
{
    if (!m_connected) {
        return;
    }

    SPDLOG_INFO("3D mouse disconnected");
    m_connected                        = false;
    m_free_rotation_recovery_requested = true;
}

bool Mouse3DController::is_connected() const
{
    return m_connected;
}

bool Mouse3DController::apply(Scene::Camera& camera, Scene::CameraTrackballController& trackball)
{
    bool camera_changed = false;
    if (m_free_rotation_recovery_requested) {
        m_free_rotation_recovery_requested = false;
        // The mouse expects the constrained orientation unless it rotates the camera freely too.
        if (!m_app_config.get<bool>("use_free_camera")) {
            trackball.recover_from_free_rotation();
            camera_changed = true;
        }
    }

    std::deque<Motion> motions;
    Settings settings;
    {
        std::scoped_lock lock(m_input_mutex);
        motions.swap(m_motion_queue);
        settings = m_settings;
    }

    for (const Motion& motion : motions) {
        if (motion.type == Motion::Type::Translation) {
            const Vec3d device_translation =
                motion.vector.cwiseProduct(settings.translation_axis_signs);
            const Vec3d translation = settings.swap_yz ?
                Vec3d(device_translation.x(), -device_translation.z(), device_translation.y()) :
                device_translation;
            if (translation.x() != 0.0 || translation.z() != 0.0) {
                // Keeps the panning speed relative to the visible part of the scene at any zoom.
                const double zoom_factor =
                    Scene::CameraProjectionParameters::orthographic_min_zoom()
                    / orthographic_equivalent_zoom(camera);
                trackball.set_target(
                    trackball.target()
                    + zoom_factor
                        * TranslationScale
                        * settings.translation_speed
                        * (translation.x() * camera.right() + translation.z() * camera.up())
                );
                // Moves the rotation center to the screen center, as the mouse pan with Shift does.
                trackball.synchronize_pivot_with_target();
            }

            if (translation.y() != 0.0) {
                trackball.update_zoom(ZoomScale * settings.zoom_speed * translation.y());
            }
        } else {
            Vec3d rotation = settings.rotation_speed
                * motion.vector.cwiseProduct(settings.rotation_axis_signs)
                * (std::numbers::pi / 180.0);
            if (settings.swap_yz) {
                rotation = Vec3d(rotation.x(), -rotation.z(), rotation.y());
            }

            trackball.rotate_around_pivot(Vec3d(rotation.x(), -rotation.z(), rotation.y()));
        }
    }

    return camera_changed || !motions.empty();
}

void Mouse3DController::on_app_config_changed(const std::string& key)
{
    if (key.starts_with("mouse_3d_")) {
        load_settings();
    }
}

void Mouse3DController::load_settings()
{
    const auto axis_sign = [this](std::string_view key)
    { return m_app_config.get<bool>(key) ? -1.0 : 1.0; };
    const Settings settings{
        .translation_speed      = m_app_config.get<double>("mouse_3d_translation_speed"),
        .translation_deadzone   = m_app_config.get<double>("mouse_3d_translation_deadzone"),
        .rotation_speed         = m_app_config.get<double>("mouse_3d_rotation_speed"),
        .rotation_deadzone      = m_app_config.get<double>("mouse_3d_rotation_deadzone"),
        .zoom_speed             = m_app_config.get<double>("mouse_3d_zoom_speed"),
        .swap_yz                = m_app_config.get<bool>("mouse_3d_swap_yz"),
        .translation_axis_signs = Vec3d(
            axis_sign("mouse_3d_invert_x"),
            axis_sign("mouse_3d_invert_y"),
            axis_sign("mouse_3d_invert_z")
        ),
        .rotation_axis_signs = Vec3d(
            axis_sign("mouse_3d_invert_pitch"),
            axis_sign("mouse_3d_invert_roll"),
            axis_sign("mouse_3d_invert_yaw")
        )
    };
    std::scoped_lock lock(m_input_mutex);
    m_settings = settings;
}

void Mouse3DController::enqueue_motion(const Motion& motion)
{
    if (m_motion_queue.size() >= MaxQueuedMotions) {
        m_motion_queue.pop_front();
    }

    m_motion_queue.push_back(motion);

    if (!m_render_request_dispatched.exchange(true)) {
        m_main_thread_dispatcher.dispatch_on_main_thread(
            [this]()
            {
                m_render_request_dispatched = false;
                Biz::Platform::PlatformServices::instance()
                    .render_request_handler()
                    .request_render();
            }
        );
    }
}

} // namespace Slic3r::App::Mouse3D
