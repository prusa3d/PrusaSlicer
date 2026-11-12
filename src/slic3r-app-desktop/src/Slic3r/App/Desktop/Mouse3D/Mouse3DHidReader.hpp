#pragma once

#include <chrono>
#include <condition_variable>
#include <jthread/JThread.hpp>
#include <mutex>
#include <optional>
#include <string_view>

struct hid_device_;

namespace Slic3r::Biz::Platform {
class IMainThreadDispatcher;
} // namespace Slic3r::Biz::Platform

namespace Slic3r::App::Mouse3D {
class Mouse3DController;
} // namespace Slic3r::App::Mouse3D

namespace Slic3r::App::Desktop {

/**
 * @brief Worker thread connecting to a 3D mouse through hidapi and reading its HID reports.
 */
class Mouse3DHidReader
{
public:
    /**
     * @brief Platform specific behavior of the reader.
     */
    struct Options
    {
        // Without it the device is searched only at the start, after disconnection and wake_up().
        std::optional<std::chrono::seconds> device_search_interval;
        // Opens only the interface of the multi-axis controller, recognized by its usage.
        bool check_interface_usage;
        // Appended to the error message when the device cannot be opened.
        std::string_view open_failure_hint;
    };

    Mouse3DHidReader(
        Mouse3D::Mouse3DController& mouse_3d_controller,
        Biz::Platform::IMainThreadDispatcher& main_thread_dispatcher,
        const Options& options
    );
    ~Mouse3DHidReader();

    Mouse3DHidReader(const Mouse3DHidReader&)            = delete;
    Mouse3DHidReader& operator=(const Mouse3DHidReader&) = delete;

    /**
     * @brief Searches for the device without waiting for the search interval.
     */
    void wake_up();

private:
    void run(const Biz::JThread::StopToken& stop_token);
    bool wait_for_device_search(const Biz::JThread::StopToken& stop_token);
    hid_device_* connect_device();
    void disconnect_device(hid_device_* device);

    Mouse3D::Mouse3DController& m_mouse_3d_controller;
    Biz::Platform::IMainThreadDispatcher& m_main_thread_dispatcher;
    const Options m_options;

    std::mutex m_wake_up_mutex;
    std::condition_variable m_wake_up_condition;
    bool m_wake_up_requested{true};

    // Declared last to be joined before the members it uses are destroyed.
    Biz::JThread::JThread m_thread;
};

} // namespace Slic3r::App::Desktop
