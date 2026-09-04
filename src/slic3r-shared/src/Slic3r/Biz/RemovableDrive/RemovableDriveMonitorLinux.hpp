#pragma once

#include "Slic3r/Biz/RemovableDrive/IRemovableDriveMonitor.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <jthread/JThread.hpp>
#include <condition_variable>
#include <mutex>

namespace Slic3r::Biz::RemovableDrive {

class RemovableDriveMonitorLinux final : public IRemovableDriveMonitor
{
public:
    RemovableDriveMonitorLinux(Platform::IMainThreadDispatcher& dispatcher);
    RemovableDriveMonitorLinux(const RemovableDriveMonitorLinux&)            = delete;
    RemovableDriveMonitorLinux(RemovableDriveMonitorLinux&&)                 = delete;
    RemovableDriveMonitorLinux& operator=(const RemovableDriveMonitorLinux&) = delete;
    RemovableDriveMonitorLinux& operator=(RemovableDriveMonitorLinux&&)      = delete;

    ~RemovableDriveMonitorLinux()
    {
        ASSERT(
            m_dispatcher.is_closed(),
            "There must be no queued events (not even in the future),"
            " because they may remember the address of this instance!"
        );
        if (m_thread.joinable()) {
            m_thread.request_stop();
            m_thread_stop_condition.notify_all();
        }
    }

    /**
     * @brief Returns path to removable drive if any exists. Prefarably one with preferred_path.
     */
    boost::filesystem::path get_path_on_removable_drive(const boost::filesystem::path& preferred_path) const override;

    /**
     * @brief Returns path to removable drive if "path" is on removable.
     */
    boost::filesystem::path get_removable_drive_path_from_path(const boost::filesystem::path& path) const override;

    /**
     * @brief Called from outside to notify Monitor to re-enumerate drives.
     */
    void on_volumes_changed() override
    {
        m_wakeup = true;
        m_thread_stop_condition.notify_all();
    }

    /**
     * @brief Returns number of removable drives.
     */
    size_t removable_drives_count() const override;

private:
    Platform::IMainThreadDispatcher& m_dispatcher;

    /**
     * @brief Re-enumerates drives in worker thread.
     * Dispatches message for each removed or added removable drive.
     */
    void update();

    void dispatch_status(const boost::filesystem::path& drive_path, RemovableDriveStatus status);

    JThread::JThread m_thread;
    std::condition_variable m_thread_stop_condition;
    mutable std::mutex m_thread_stop_mutex;
    std::atomic<bool> m_wakeup{true};
    mutable std::mutex m_inside_update_mutex;

    std::vector<DriveData> m_current_drives;
    mutable std::mutex m_drives_mutex;
};
} // namespace Slic3r::Biz::RemovableDrive
