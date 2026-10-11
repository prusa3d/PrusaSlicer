#pragma once

#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/IAppConfigChangedListener.hpp"
#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Domain/Types.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <string>

namespace Slic3r::Biz::Platform {
class IMainThreadDispatcher;
} // namespace Slic3r::Biz::Platform

namespace Slic3r::App {
class AppConfig;
} // namespace Slic3r::App

namespace Slic3r::App::Scene {
class Camera;
class CameraTrackballController;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Mouse3D {

/**
 * @brief Queues the 3D mouse (3Dconnexion) input fed by the backends from any thread and applies
 * it to the camera at the beginning of every frame. Unless stated otherwise, methods must be
 * called from the main thread.
 */
class Mouse3DController : public IAppConfigChangedListener
{
public:
    Mouse3DController(
        AppConfig& app_config,
        AppConfigInteractor& app_config_interactor,
        Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher
    );

    /**
     * @brief Decodes a HID report of the device and queues its motion. Callable from any thread.
     */
    void handle_hid_report(std::span<const std::uint8_t> report);

    /**
     * @brief Queues the raw axes of a motion reported by the driver. Callable from any thread.
     */
    void handle_axis_input(const Domain::Vec3d& translation, const Domain::Vec3d& rotation);

    /**
     * @brief Ignores the device input while the application is inactive. Callable from any thread.
     */
    void set_application_active(bool active);

    /**
     * @brief Returns true if the mouse wheel event was emitted by the Windows 3Dconnexion driver
     * for the rotation around the X axis and has to be ignored.
     */
    bool consume_driver_mouse_wheel_event();

    void device_connected(const std::string& device_name);
    void device_disconnected();

    bool is_connected() const;

    /**
     * @brief Applies the queued motion to the camera.
     * @return True if the camera has changed.
     */
    bool apply(Scene::Camera& camera, Scene::CameraTrackballController& trackball);

    void on_app_config_changed(const std::string& key) override;

private:
    struct Settings
    {
        double translation_speed;
        double translation_deadzone;
        double rotation_speed;
        double rotation_deadzone;
        double zoom_speed;
        bool swap_yz;
        // 1 or -1 for every device axis, the rotation axes are pitch, roll and yaw.
        Domain::Vec3d translation_axis_signs;
        Domain::Vec3d rotation_axis_signs;
    };

    struct Motion
    {
        enum class Type : std::uint8_t
        {
            Translation,
            Rotation
        };

        Type type;
        Domain::Vec3d vector;
    };

    void load_settings();
    void enqueue_motion(const Motion& motion);

    AppConfig& m_app_config;
    Biz::Platform::IMainThreadDispatcher& m_main_thread_dispatcher;

    // Guards the members accessed by the backends from their threads.
    std::mutex m_input_mutex;
    std::deque<Motion> m_motion_queue;
    Settings m_settings;
    unsigned int m_driver_mouse_wheel_counter{0};

    std::atomic<bool> m_application_active{true};
    std::atomic<bool> m_render_request_dispatched{false};

    bool m_connected{false};
    bool m_free_rotation_recovery_requested{false};

    Biz::ListenerScope<IAppConfigChangedListener, AppConfigInteractor, Mouse3DController>
        m_app_config_changed_listener_scope;
};

} // namespace Slic3r::App::Mouse3D
