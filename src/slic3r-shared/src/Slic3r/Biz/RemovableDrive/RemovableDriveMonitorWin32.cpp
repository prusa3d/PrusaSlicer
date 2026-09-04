#include "RemovableDriveMonitorWin32.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"

#include <string>
#include <algorithm>
#include <windows.h>
#include <shlwapi.h>
#include <cwchar>
#include <boost/nowide/convert.hpp>

namespace Slic3r::Biz::RemovableDrive {

namespace {
std::vector<DriveData> search_for_removable_drives()
{
    // Get logical drives flags by letter in alphabetical order.
    DWORD drives_mask = ::GetLogicalDrives();

    // Allocate the buffers before the loop.
    std::wstring volume_name;
    std::wstring file_system_name;
    // Iterate the Windows drives from 'C' to 'Z'
    std::vector<DriveData> current_drives;
    // Skip A and B drives.
    drives_mask >>= 2;
    for (char drive = 'C'; drive <= 'Z'; ++drive, drives_mask >>= 1)
        if (drives_mask & 1) {
            std::string path{drive, ':'};
            UINT drive_type = ::GetDriveTypeA(path.c_str());
            // DRIVE_REMOVABLE on W are sd cards and usb thumbnails (not usb harddrives)
            if (drive_type == DRIVE_REMOVABLE) {
                // get name of drive
                std::wstring wpath = boost::nowide::widen(path);
                volume_name.resize(MAX_PATH + 1);
                file_system_name.resize(MAX_PATH + 1);
                BOOL error = ::GetVolumeInformationW(
                    wpath.c_str(),
                    volume_name.data(),
                    sizeof(volume_name),
                    nullptr,
                    nullptr,
                    nullptr,
                    file_system_name.data(),
                    sizeof(file_system_name)
                );
                if (error != 0) {
                    volume_name.erase(volume_name.begin() + wcslen(volume_name.c_str()), volume_name.end());
                    if (!file_system_name.empty()) {
                        ULARGE_INTEGER free_space;
                        ::GetDiskFreeSpaceExW(wpath.c_str(), &free_space, nullptr, nullptr);
                        if (free_space.QuadPart > 0) {
                            path += "\\";
                            current_drives.emplace_back(DriveData{boost::nowide::narrow(volume_name), path});
                        }
                    }
                }
            }
        }
    return current_drives;
}

} // namespace

RemovableDriveMonitorWin32::RemovableDriveMonitorWin32(Platform::IMainThreadDispatcher& dispatcher) :
    m_dispatcher(dispatcher)
{
    m_thread = JThread::JThread(
        [this](JThread::StopToken stop_token)
        {
            while (true) {
                std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
                m_thread_stop_condition
                    .wait(lck, [this, stop_token] { return stop_token.stop_requested() || m_wakeup; });
                if (stop_token.stop_requested()) {
                    return;
                }
                this->update();
                m_wakeup = false;
                if (stop_token.stop_requested()) {
                    return;
                }
            }
        }
    );
}

void RemovableDriveMonitorWin32::update()
{
    std::unique_lock<std::mutex> inside_update_lock(m_inside_update_mutex, std::defer_lock);

    if (inside_update_lock.try_lock()) {
        // Got the lock without waiting. That means, the update was not running.
        // Run the update.
        std::vector<DriveData> current_drives = search_for_removable_drives();
        // Post update events.
        std::scoped_lock<std::mutex> lock(m_drives_mutex);

        std::sort(current_drives.begin(), current_drives.end());
        SPDLOG_INFO("Removable Drive Update: {}", current_drives != m_current_drives);
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

boost::filesystem::path RemovableDriveMonitorWin32::get_path_on_removable_drive(const boost::filesystem::path& preferred_path) const
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

boost::filesystem::path RemovableDriveMonitorWin32::get_removable_drive_path_from_path(const boost::filesystem::path& path) const
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

size_t RemovableDriveMonitorWin32::removable_drives_count() const
{
    {
        std::scoped_lock<std::mutex> lock(m_drives_mutex);
        return m_current_drives.size();
    }
}

void RemovableDriveMonitorWin32::dispatch_status(const boost::filesystem::path& drive_path, RemovableDriveStatus status)
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
