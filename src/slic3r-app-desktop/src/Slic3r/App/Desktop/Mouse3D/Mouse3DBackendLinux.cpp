#include "Slic3r/App/Desktop/Mouse3D/Mouse3DBackend.hpp"
#include "Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.hpp"

#include <chrono>

namespace Slic3r::App::Desktop {

namespace {

// There are no device notifications, a device connected later is found by the periodic search.
// Usages are not reported on Linux, the first interface which can be opened is used.
constexpr Mouse3DHidReader::Options HidReaderOptions{
    .device_search_interval = std::chrono::seconds{2},
    .check_interface_usage  = false,
    .open_failure_hint      = " You may need to update /etc/udev/rules.d",
};

class Mouse3DBackendLinux final : public IMouse3DBackend
{
public:
    Mouse3DBackendLinux(
        Mouse3D::Mouse3DController& mouse_3d_controller,
        Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher
    ) :
        m_hid_reader(mouse_3d_controller, main_thread_dispatcher, HidReaderOptions)
    {}

private:
    Mouse3DHidReader m_hid_reader;
};

} // namespace

namespace Mouse3DBackendFactory {

std::unique_ptr<IMouse3DBackend> create_mouse_3d_backend(
    Mouse3D::Mouse3DController& mouse_3d_controller,
    Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher
)
{
    return std::make_unique<Mouse3DBackendLinux>(mouse_3d_controller, main_thread_dispatcher);
}

} // namespace Mouse3DBackendFactory

} // namespace Slic3r::App::Desktop
