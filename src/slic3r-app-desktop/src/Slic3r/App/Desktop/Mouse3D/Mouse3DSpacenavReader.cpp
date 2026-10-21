#include "Slic3r/App/Desktop/Mouse3D/Mouse3DSpacenavReader.hpp"

#include "Slic3r/App/Mouse3D/Mouse3DController.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Log.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <utility>

#include <poll.h>
#include <pthread.h>
#include <unistd.h>

// Included last, it may include X11 headers defining macros like None or Status.
#include <spnav.h>

using Slic3r::Domain::Vec3d;

namespace Slic3r::App::Desktop {

namespace {

constexpr std::chrono::seconds ReconnectInterval{2};
// The timeout keeps the thread responsive to the stop request.
constexpr int PollTimeoutMs = 100;

bool daemon_socket_exists()
{
    // libspnav leaks memory on every failed connection attempt.
    const char* socket_path = std::getenv("SPNAV_SOCKET");
    return ::access(socket_path != nullptr ? socket_path : "/var/run/spnav.sock", F_OK) == 0;
}

std::optional<std::string> query_device_name()
{
    std::array<char, 256> device_name{};
    if (spnav_dev_name(device_name.data(), static_cast<int>(device_name.size())) < 0) {
        return std::nullopt;
    }

    return std::string(device_name.data());
}

void block_sigpipe()
{
    // libspnav writes to the socket of a stopped daemon, the signal would kill the application.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);
}

} // namespace

Mouse3DSpacenavReader::Mouse3DSpacenavReader(
    Mouse3D::Mouse3DController& mouse_3d_controller,
    Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher,
    DeviceProvidedCallback device_provided_callback
) :
    m_mouse_3d_controller(mouse_3d_controller),
    m_main_thread_dispatcher(main_thread_dispatcher),
    m_device_provided_callback(std::move(device_provided_callback)),
    m_thread([this](Biz::JThread::StopToken stop_token) { run(stop_token); })
{}

Mouse3DSpacenavReader::~Mouse3DSpacenavReader()
{
    {
        std::scoped_lock lock(m_stop_mutex);
        m_thread.request_stop();
    }

    m_stop_condition.notify_all();
}

void Mouse3DSpacenavReader::run(const Biz::JThread::StopToken& stop_token)
{
    block_sigpipe();
    while (!stop_token.stop_requested()) {
        if (!m_daemon_connected) {
            if (!connect_daemon()) {
                set_device(std::nullopt);
                wait_for_reconnect(stop_token);
            }

            continue;
        }

        pollfd daemon_socket{spnav_fd(), POLLIN, 0};
        const int poll_result = ::poll(&daemon_socket, 1, PollTimeoutMs);
        if (poll_result < 0 && errno == EINTR) {
            continue;
        }

        // libspnav reports the closed connection as no event, it is detected here.
        if (poll_result < 0 || (daemon_socket.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
            disconnect_daemon();
        } else if (poll_result > 0) {
            read_events();
        }
    }

    if (m_daemon_connected) {
        spnav_close();
    }
}

void Mouse3DSpacenavReader::wait_for_reconnect(const Biz::JThread::StopToken& stop_token)
{
    std::unique_lock lock(m_stop_mutex);
    m_stop_condition
        .wait_for(lock, ReconnectInterval, [&stop_token]() { return stop_token.stop_requested(); });
}

bool Mouse3DSpacenavReader::connect_daemon()
{
    if (!daemon_socket_exists()) {
        log_unavailability("spacenavd is not running, the 3D mouse is read through hidapi");
        return false;
    }

    if (spnav_open() == -1) {
        log_unavailability("Connection to spacenavd failed, the 3D mouse is read through hidapi");
        return false;
    }

    const int protocol = spnav_protocol();
    SPDLOG_INFO("Connected to spacenavd, protocol {}", protocol);

    m_daemon_connected      = true;
    m_unavailability_logged = false;
    if (protocol < 1) {
        // spacenavd older than 1.0 does not report the devices, the device is assumed to be
        // provided and is reported as connected by its first motion.
        m_device_provided_callback(true);
    } else {
        if (spnav_evmask(SPNAV_EVMASK_MOTION | SPNAV_EVMASK_DEV) == -1) {
            SPDLOG_WARN("Selection of the spacenavd events failed");
        }

        set_device(query_device_name());
    }

    return true;
}

void Mouse3DSpacenavReader::disconnect_daemon()
{
    spnav_close();
    m_daemon_connected = false;
    SPDLOG_INFO("Connection to spacenavd closed");
    set_device(std::nullopt);
}

void Mouse3DSpacenavReader::read_events()
{
    spnav_event event;
    while (spnav_poll_event(&event) != 0) {
        if (event.type == SPNAV_EVENT_DEV) {
            set_device(query_device_name());
        } else if (event.type == SPNAV_EVENT_MOTION) {
            handle_motion(event.motion);
        }
    }
}

void Mouse3DSpacenavReader::handle_motion(const spnav_event_motion& motion)
{
    if (!m_device_available) {
        // The device query fails when it is answered by an event, the motion proves the device.
        set_device(query_device_name().value_or("spacenavd"));
    }

    // spacenavd swaps and inverts the Y and Z axes, they are converted back to the HID convention.
    m_mouse_3d_controller.handle_axis_input(
        Vec3d(motion.x, -motion.z, -motion.y),
        Vec3d(motion.rx, -motion.rz, -motion.ry)
    );
}

void Mouse3DSpacenavReader::set_device(const std::optional<std::string>& device_name)
{
    if (device_name.has_value()) {
        if (!m_device_available) {
            m_device_available = true;
            m_device_provided_callback(true);
            SPDLOG_INFO("3D mouse is read through spacenavd: {}", *device_name);
            m_main_thread_dispatcher.dispatch_on_main_thread(
                [&mouse_3d_controller  = m_mouse_3d_controller,
                 connected_device_name = *device_name]()
                { mouse_3d_controller.device_connected(connected_device_name); }
            );
        }

        return;
    }

    if (m_device_available) {
        m_device_available = false;
        m_main_thread_dispatcher.dispatch_on_main_thread(
            [&mouse_3d_controller = m_mouse_3d_controller]()
            { mouse_3d_controller.device_disconnected(); }
        );
    }

    m_device_provided_callback(false);
}

void Mouse3DSpacenavReader::log_unavailability(std::string_view message)
{
    if (!m_unavailability_logged) {
        SPDLOG_INFO("{}", message);
        m_unavailability_logged = true;
    }
}

} // namespace Slic3r::App::Desktop
