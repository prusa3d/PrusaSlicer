#pragma once

#include "Slic3r/Biz/RemovableDrive/IRemovableDriveMonitor.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"

#include <jthread/JThread.hpp>
#include <condition_variable>
#include <mutex>

namespace Slic3r::Biz::RemovableDrive {

class RemovableDriveMonitorMac final : public IRemovableDriveMonitor
{
public:
    RemovableDriveMonitorMac(Platform::IMainThreadDispatcher& dispatcher);
    RemovableDriveMonitorMac(const RemovableDriveMonitorMac&)            = delete;
    RemovableDriveMonitorMac(RemovableDriveMonitorMac&&)                 = delete;
    RemovableDriveMonitorMac& operator=(const RemovableDriveMonitorMac&) = delete;
    RemovableDriveMonitorMac& operator=(RemovableDriveMonitorMac&&)      = delete;

    ~RemovableDriveMonitorMac()
    {
        ASSERT(
            m_dispatcher.is_closed(),
            "There must be no queued events (not even in the future),"
            " because they may remember the address of this instance!"
        );
        relase_observer();
        relase_enumerator();
        if (m_thread.joinable()) {
            m_thread.request_stop();
            m_thread_stop_condition.notify_all();
            // we don't want to destroy mutexes (member variables) if the thread can still run (and use them)
            m_thread.join();
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

    // Objective-C objects

    /**
     * @brief Objective-C observer that calls on_volumes_changed whe drives change.
     */
    void* m_observer;
    void init_observer(std::function<void()> callback);
    void relase_observer();

    /**
     * @brief Objective-C enumerator - inner function of update.
     */
    void* m_enumerator;
    void init_enumerator();
    void relase_enumerator();
    void list_devices(std::vector<DriveData>& out) const;
};
} // namespace Slic3r::Biz::RemovableDrive
