#include "RemovableDriveMonitorMac.hpp"

#include "Slic3r/Assert.hpp"

#include <string>
#include <algorithm>

namespace Slic3r::Biz::RemovableDrive {

RemovableDriveMonitorMac::RemovableDriveMonitorMac(Platform::IMainThreadDispatcher& dispatcher) :
    m_dispatcher(dispatcher)
{
    init_observer([this]() { on_volumes_changed(); });
    init_enumerator();
    m_thread = JThread::JThread(
        [this](JThread::StopToken stop_token)
        {
            while (true) {
                std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
                m_thread_stop_condition.wait_for(
                    lck,
                    std::chrono::seconds(2),
                    [this, stop_token] { return stop_token.stop_requested() || m_wakeup; }
                );
                if (stop_token.stop_requested()) {
                    return;
                }
                this->update();
                m_wakeup = false;
            }
        }
    );
}

void RemovableDriveMonitorMac::update()
{
    std::unique_lock<std::mutex> inside_update_lock(m_inside_update_mutex, std::defer_lock);

    if (inside_update_lock.try_lock()) {
        // Got the lock without waiting. That means, the update was not running.
        // Run the update.
        std::vector<DriveData> current_drives;
        list_devices(current_drives);
        // Post update events.
        std::scoped_lock<std::mutex> lock(m_drives_mutex);

        std::sort(current_drives.begin(), current_drives.end());
        // SPDLOG_INFO("Removable Drive Update: {}", current_drives != m_current_drives);
        if (current_drives != m_current_drives) {
            // TODO dispatch changes
            auto changes = get_drive_changes(m_current_drives, current_drives);
            for (const auto& added : changes.first) {
                dispatch_status(added.path, RemovableDriveStatus::Inserted);
            }
            for (const auto& removed : changes.second) {
                dispatch_status(removed.path, RemovableDriveStatus::Removed);
            }
        }
        m_current_drives = std::move(current_drives);

    } else {
        // Acquiring the m_iniside_update lock failed, therefore another update is running.
        // Just block until the other instance of update() finishes.
        inside_update_lock.lock();
    }
}

boost::filesystem::path RemovableDriveMonitorMac::get_path_on_removable_drive(const boost::filesystem::path& preferred_path) const
{
    boost::filesystem::path result = get_removable_drive_path_from_path(preferred_path);
    if (!result.empty()) {
        return result;
    }
    {
        std::scoped_lock<std::mutex> lock(m_drives_mutex);
        if (m_current_drives.empty()) {
            return boost::filesystem::path();
        }
        return m_current_drives.front().path;
    }
}

boost::filesystem::path RemovableDriveMonitorMac::get_removable_drive_path_from_path(const boost::filesystem::path& path) const
{
    {
        std::scoped_lock<std::mutex> lock(m_drives_mutex);
        auto it_drive_data = find_drive(m_current_drives, path);
        if (it_drive_data != m_current_drives.end()) {
            return it_drive_data->path;
        } else {
            return {};
        }
    }
}

size_t RemovableDriveMonitorMac::removable_drives_count() const
{
    {
        std::scoped_lock<std::mutex> lock(m_drives_mutex);
        return m_current_drives.size();
    }
}

void RemovableDriveMonitorMac::dispatch_status(const boost::filesystem::path& drive_path, RemovableDriveStatus status)
{
    m_dispatcher.dispatch_on_main_thread(
        [this, drive_path, status]() mutable
        {
            this->invoke_listeners<IRemovableDriveStatusListener>(
                [drive_path, status](auto* listener) mutable
                { listener->on_removable_drive_status_changed(drive_path, status); }
            );
        }
    );
}

} // namespace Slic3r::Biz::RemovableDrive
