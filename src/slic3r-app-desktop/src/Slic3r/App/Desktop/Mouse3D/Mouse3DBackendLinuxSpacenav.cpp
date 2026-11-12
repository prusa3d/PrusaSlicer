#include "Slic3r/App/Desktop/Mouse3D/Mouse3DBackend.hpp"
#include "Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.hpp"
#include "Slic3r/App/Desktop/Mouse3D/Mouse3DSpacenavReader.hpp"

#include <chrono>
#include <optional>

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
        m_mouse_3d_controller(mouse_3d_controller),
        m_main_thread_dispatcher(main_thread_dispatcher),
        m_spacenav_reader(
            mouse_3d_controller,
            main_thread_dispatcher,
            [this](bool device_provided) { set_hid_reader_running(!device_provided); }
        )
    {}

private:
    // Called from the spacenavd reader thread.
    void set_hid_reader_running(bool running)
    {
        if (!running) {
            m_hid_reader.reset();
        } else if (!m_hid_reader.has_value()) {
            m_hid_reader.emplace(m_mouse_3d_controller, m_main_thread_dispatcher, HidReaderOptions);
        }
    }

    Mouse3D::Mouse3DController& m_mouse_3d_controller;
    Biz::Platform::IMainThreadDispatcher& m_main_thread_dispatcher;
    // spacenavd does not prevent reading the device through hidapi, so the reader exists only while
    // spacenavd does not provide the device, otherwise every motion would be read twice.
    std::optional<Mouse3DHidReader> m_hid_reader;
    // Declared last to be stopped before the hidapi reader it controls.
    Mouse3DSpacenavReader m_spacenav_reader;
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
