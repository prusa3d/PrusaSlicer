#pragma once

#include <condition_variable>
#include <functional>
#include <jthread/JThread.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

struct spnav_event_motion;

namespace Slic3r::Biz::Platform {
class IMainThreadDispatcher;
} // namespace Slic3r::Biz::Platform

namespace Slic3r::App::Mouse3D {
class Mouse3DController;
} // namespace Slic3r::App::Mouse3D

namespace Slic3r::App::Desktop {

/**
 * @brief Worker thread reading a 3D mouse through the spacenavd daemon (libspnav).
 */
class Mouse3DSpacenavReader
{
public:
    /**
     * @brief Called from the worker thread before the device is reported as connected and after
     * it is reported as disconnected, possibly repeatedly with the same value.
     *
     * @note With spacenavd older than 1.0 it is called right after connecting to the daemon, even
     * if no device is ever reported.
     */
    using DeviceProvidedCallback = std::function<void(bool device_provided)>;

    Mouse3DSpacenavReader(
        Mouse3D::Mouse3DController& mouse_3d_controller,
        Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher,
        DeviceProvidedCallback device_provided_callback
    );
    ~Mouse3DSpacenavReader();

    Mouse3DSpacenavReader(const Mouse3DSpacenavReader&)            = delete;
    Mouse3DSpacenavReader& operator=(const Mouse3DSpacenavReader&) = delete;

private:
    void run(const Biz::JThread::StopToken& stop_token);
    void wait_for_reconnect(const Biz::JThread::StopToken& stop_token);
    bool connect_daemon();
    void disconnect_daemon();
    void read_events();
    void handle_motion(const spnav_event_motion& motion);
    void set_device(const std::optional<std::string>& device_name);
    void log_unavailability(std::string_view message);

    Mouse3D::Mouse3DController& m_mouse_3d_controller;
    Biz::Platform::IMainThreadDispatcher& m_main_thread_dispatcher;
    const DeviceProvidedCallback m_device_provided_callback;

    // Accessed only by the worker thread.
    bool m_daemon_connected{false};
    bool m_device_available{false};
    bool m_unavailability_logged{false};

    std::mutex m_stop_mutex;
    std::condition_variable m_stop_condition;

    // Declared last to be joined before the members it uses are destroyed.
    Biz::JThread::JThread m_thread;
};

} // namespace Slic3r::App::Desktop
