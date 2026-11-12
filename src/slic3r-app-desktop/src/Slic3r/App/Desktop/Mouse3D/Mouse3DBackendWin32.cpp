#include "Slic3r/App/Desktop/Mouse3D/Mouse3DBackend.hpp"
#include "Slic3r/App/Desktop/Mouse3D/Mouse3DDevices.hpp"
#include "Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.hpp"
#include "Slic3r/App/Mouse3D/Mouse3DController.hpp"
#include "Slic3r/Log.hpp"

#include <cstdio>
#include <hidapi.h>
#include <optional>
#include <windows.h>

namespace Slic3r::App::Desktop {

namespace {

struct DeviceIds
{
    unsigned short vendor_id;
    unsigned short product_id;
};

/**
 * @brief Returns the ids of a supported 3D mouse parsed from a HID interface path like
 * "\\?\HID#VID_046D&PID_C626&...".
 */
std::optional<DeviceIds> parse_mouse_3d_device_path(const std::string& device_path)
{
    DeviceIds device_ids{};
    const int parsed_count = std::sscanf(
        device_path.c_str(),
        "\\\\?\\HID#VID_%hx&PID_%hx&",
        &device_ids.vendor_id,
        &device_ids.product_id
    );
    if (parsed_count != 2 || !is_mouse_3d_device(device_ids.vendor_id, device_ids.product_id)) {
        return std::nullopt;
    }
    return device_ids;
}

std::optional<std::string> detect_connected_device()
{
    if (hid_init() != 0) {
        SPDLOG_ERROR("Unable to initialize hidapi library");
        return std::nullopt;
    }

    std::optional<std::string> device_name;
    hid_device_info* device_infos = hid_enumerate(0, 0);
    for (const hid_device_info* device_info = device_infos; device_info != nullptr;
         device_info                        = device_info->next)
    {
        // A wireless receiver exposes several interfaces, the device is the multi-axis controller.
        if (is_mouse_3d_device(device_info->vendor_id, device_info->product_id)
            && device_info->usage_page == 1
            && device_info->usage == 8)
        {
            device_name = mouse_3d_device_name(device_info->vendor_id, device_info->product_id);
            break;
        }
    }

    hid_free_enumeration(device_infos);
    hid_exit();

    return device_name;
}

// The attached devices are reported by WM_DEVICECHANGE, they are not searched periodically.
constexpr Mouse3DHidReader::Options HidReaderOptions{
    .device_search_interval = std::nullopt,
    .check_interface_usage  = true,
    .open_failure_hint      = "",
};

class Mouse3DBackendWin32 final : public IMouse3DBackend
{
public:
    Mouse3DBackendWin32(
        Mouse3D::Mouse3DController& mouse_3d_controller,
        Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher
    ) :
        m_mouse_3d_controller(mouse_3d_controller)
    {
        // Raw input is not delivered in a remote desktop session, hidapi reads the device there.
        if (::GetSystemMetrics(SM_REMOTESESSION) != 0) {
            m_hid_reader.emplace(mouse_3d_controller, main_thread_dispatcher, HidReaderOptions);
        } else if (const std::optional<std::string> device_name = detect_connected_device()) {
            mouse_3d_controller.device_connected(*device_name);
        }
    }

    void handle_hid_device_attached(const std::string& device_path) override
    {
        const std::optional<DeviceIds> device_ids = parse_mouse_3d_device_path(device_path);
        if (!device_ids.has_value()) {
            return;
        }

        // Reported once for every HID interface of the device, the repeated reports are ignored.
        if (m_hid_reader.has_value()) {
            m_hid_reader->wake_up();
        } else {
            m_mouse_3d_controller.device_connected(
                mouse_3d_device_name(device_ids->vendor_id, device_ids->product_id)
            );
        }
    }

    void handle_hid_device_detached(const std::string& device_path) override
    {
        // The hidapi reader detects the disconnection by itself.
        if (!m_hid_reader.has_value() && parse_mouse_3d_device_path(device_path).has_value()) {
            m_mouse_3d_controller.device_disconnected();
        }
    }

private:
    Mouse3D::Mouse3DController& m_mouse_3d_controller;
    std::optional<Mouse3DHidReader> m_hid_reader;
};

} // namespace

namespace Mouse3DBackendFactory {

std::unique_ptr<IMouse3DBackend> create_mouse_3d_backend(
    Mouse3D::Mouse3DController& mouse_3d_controller,
    Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher
)
{
    return std::make_unique<Mouse3DBackendWin32>(mouse_3d_controller, main_thread_dispatcher);
}

} // namespace Mouse3DBackendFactory

} // namespace Slic3r::App::Desktop
