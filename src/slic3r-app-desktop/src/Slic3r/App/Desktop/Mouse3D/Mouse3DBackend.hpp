#pragma once

#include <memory>
#include <string>

namespace Slic3r::Biz::Platform {
class IMainThreadDispatcher;
} // namespace Slic3r::Biz::Platform

namespace Slic3r::App::Mouse3D {
class Mouse3DController;
} // namespace Slic3r::App::Mouse3D

namespace Slic3r::App::Desktop {

/**
 * @brief Connects the 3D mouse (3Dconnexion) of the operating system to Mouse3DController. On
 * Windows, DesktopApp reads the raw input of the device directly.
 */
class IMouse3DBackend
{
public:
    virtual ~IMouse3DBackend() = default;

    /**
     * @brief Handles the path of an attached HID device interface, reported only on Windows.
     */
    virtual void handle_hid_device_attached(const std::string& device_path) {}

    /**
     * @brief Handles the path of a detached HID device interface, reported only on Windows.
     */
    virtual void handle_hid_device_detached(const std::string& device_path) {}
};

namespace Mouse3DBackendFactory {

std::unique_ptr<IMouse3DBackend> create_mouse_3d_backend(
    Mouse3D::Mouse3DController& mouse_3d_controller,
    Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher
);

} // namespace Mouse3DBackendFactory

} // namespace Slic3r::App::Desktop
