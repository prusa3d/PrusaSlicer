#include "Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.hpp"

#include "Slic3r/App/Desktop/Mouse3D/Mouse3DDevices.hpp"
#include "Slic3r/App/Mouse3D/Mouse3DController.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Log.hpp"

#include <array>
#include <boost/algorithm/string/trim.hpp>
#include <boost/nowide/convert.hpp>
#include <cstdint>
#include <hidapi.h>
#include <span>
#include <string>
#include <vector>

namespace Slic3r::App::Desktop {

namespace {

struct DeviceInterface
{
    std::string path;
    unsigned short vendor_id;
    unsigned short product_id;
    unsigned short usage_page;
    unsigned short usage;
};

std::string read_device_name(hid_device* device)
{
    std::array<wchar_t, 1024> manufacturer{};
    hid_get_manufacturer_string(device, manufacturer.data(), manufacturer.size());
    std::array<wchar_t, 1024> product{};
    hid_get_product_string(device, product.data(), product.size());
    // Some devices report the strings with surrounding white spaces (#3479).
    return boost::algorithm::trim_copy(boost::nowide::narrow(manufacturer.data()))
        + "/"
        + boost::algorithm::trim_copy(boost::nowide::narrow(product.data()));
}

} // namespace

Mouse3DHidReader::Mouse3DHidReader(
    Mouse3D::Mouse3DController& mouse_3d_controller,
    Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher,
    const Options& options
) :
    m_mouse_3d_controller(mouse_3d_controller),
    m_main_thread_dispatcher(main_thread_dispatcher),
    m_options(options),
    m_thread([this](Biz::JThread::StopToken stop_token) { run(stop_token); })
{}

Mouse3DHidReader::~Mouse3DHidReader()
{
    {
        std::scoped_lock lock(m_wake_up_mutex);
        m_thread.request_stop();
    }

    m_wake_up_condition.notify_all();
}

void Mouse3DHidReader::wake_up()
{
    {
        std::scoped_lock lock(m_wake_up_mutex);
        m_wake_up_requested = true;
    }

    m_wake_up_condition.notify_all();
}

void Mouse3DHidReader::run(const Biz::JThread::StopToken& stop_token)
{
    if (hid_init() != 0) {
        SPDLOG_ERROR("Unable to initialize hidapi library");
        return;
    }

    hid_device* device = nullptr;
    while (!stop_token.stop_requested()) {
        if (device == nullptr) {
            if (wait_for_device_search(stop_token)) {
                device = connect_device();
            }
        } else {
            std::array<std::uint8_t, 13> report{};
            // The timeout keeps the thread responsive to the stop request.
            const int report_length = hid_read_timeout(device, report.data(), report.size(), 100);
            if (report_length < 0) {
                disconnect_device(device);
                device = nullptr;
            } else if (report_length > 0) {
                m_mouse_3d_controller.handle_hid_report(
                    std::span<const std::uint8_t>(report.data(), static_cast<size_t>(report_length))
                );
            }
        }
    }

    if (device != nullptr) {
        hid_close(device);
    }

    hid_exit();
}

bool Mouse3DHidReader::wait_for_device_search(const Biz::JThread::StopToken& stop_token)
{
    std::unique_lock lock(m_wake_up_mutex);
    const auto search_requested = [this, &stop_token]()
    { return stop_token.stop_requested() || m_wake_up_requested; };
    if (m_options.device_search_interval.has_value()) {
        m_wake_up_condition.wait_for(lock, *m_options.device_search_interval, search_requested);
    } else {
        m_wake_up_condition.wait(lock, search_requested);
    }

    m_wake_up_requested = false;
    return !stop_token.stop_requested();
}

hid_device* Mouse3DHidReader::connect_device()
{
    std::vector<DeviceInterface> device_interfaces;
    hid_device_info* device_infos = hid_enumerate(0, 0);
    for (const hid_device_info* device_info = device_infos; device_info != nullptr;
         device_info                        = device_info->next)
    {
        if (is_mouse_3d_device(device_info->vendor_id, device_info->product_id)) {
            device_interfaces.push_back(
                {device_info->path,
                 device_info->vendor_id,
                 device_info->product_id,
                 device_info->usage_page,
                 device_info->usage}
            );
        }
    }
    hid_free_enumeration(device_infos);

    // A wireless receiver exposes several interfaces, the device is the multi-axis controller
    // (usage page 1, usage 8).
    for (const DeviceInterface& device_interface : device_interfaces) {
        if (m_options.check_interface_usage
            && (device_interface.usage_page != 1 || device_interface.usage != 8))
        {
            continue;
        }

        hid_device* device = hid_open_path(device_interface.path.c_str());
        if (device == nullptr) {
            SPDLOG_ERROR(
                "3Dconnexion device cannot be opened: {}.{}",
                device_interface.path,
                m_options.open_failure_hint
            );
            continue;
        }

        const std::string device_name = read_device_name(device);
        SPDLOG_INFO(
            "3Dconnexion device connected: {}, vendor id {:#06x}, product id {:#06x}, path {}",
            device_name,
            device_interface.vendor_id,
            device_interface.product_id,
            device_interface.path
        );
        m_main_thread_dispatcher.dispatch_on_main_thread(
            [&mouse_3d_controller = m_mouse_3d_controller, device_name]()
            { mouse_3d_controller.device_connected(device_name); }
        );

        return device;
    }

    return nullptr;
}

void Mouse3DHidReader::disconnect_device(hid_device* device)
{
    hid_close(device);
    SPDLOG_INFO("3Dconnexion device disconnected");
    m_main_thread_dispatcher.dispatch_on_main_thread(
        [&mouse_3d_controller = m_mouse_3d_controller]()
        { mouse_3d_controller.device_disconnected(); }
    );

    // Searches for the device again without waiting for the search interval.
    std::scoped_lock lock(m_wake_up_mutex);
    m_wake_up_requested = true;
}

} // namespace Slic3r::App::Desktop
