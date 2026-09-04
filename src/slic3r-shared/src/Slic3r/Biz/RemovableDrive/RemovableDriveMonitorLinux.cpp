#include <chrono>
#include "RemovableDriveMonitorLinux.hpp"
#include "Slic3r/Platform.hpp"

#include "Slic3r/Assert.hpp"

#include <chrono>
#include <string>
#include <algorithm>
#include <glob.h>
#include <cstdlib>
#include <unistd.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <boost/filesystem.hpp>

namespace Slic3r::Biz::RemovableDrive {

namespace {
bool compare_filesystem_id(const std::string& path_a, const std::string& path_b)
{
    struct stat buf;
    stat(path_a.c_str(), &buf);
    dev_t id_a = buf.st_dev;
    stat(path_b.c_str(), &buf);
    dev_t id_b = buf.st_dev;
    return id_a == id_b;
}

void inspect_file(const std::string& path, const std::string& parent_path, std::vector<DriveData>& out)
{
    // confirms if the file is removable drive and adds it to vector

    if (
            // Chromium mounts removable drives in a way that produces the same device ID.
            platform_flavor() == PlatformFlavor::LinuxOnChromium ||
            // If not same file system - could be removable drive.
            ! compare_filesystem_id(path, parent_path))
    {
        // free space
        boost::system::error_code ec;
        boost::filesystem::space_info si = boost::filesystem::space(path, ec);
        if (!ec && si.available != 0) {
            // user id
            struct stat buf;
            stat(path.c_str(), &buf);
            uid_t uid = buf.st_uid;
            if (getuid() == uid)
                out.emplace_back(DriveData{boost::filesystem::path(path).stem().string(), path});
        }
    }
}

void search_path(const std::string& path, const std::string& parent_path, std::vector<DriveData>& out)
{
    glob_t globbuf;
    globbuf.gl_offs = 2;
    int error       = glob(path.c_str(), GLOB_TILDE, NULL, &globbuf);
    if (error == 0) {
        for (size_t i = 0; i < globbuf.gl_pathc; ++i)
            inspect_file(globbuf.gl_pathv[i], parent_path, out);
    } else {
        // if error - path probably doesnt exists so function just exits
        // std::cout<<"glob error "<< error<< "\n";
    }
    globfree(&globbuf);
}

std::vector<DriveData> search_for_removable_drives()
{
    std::vector<DriveData> current_drives;

    if (platform_flavor() == PlatformFlavor::LinuxOnChromium) {
        // ChromeOS specific: search /mnt/chromeos/removable/* folder
        search_path("/mnt/chromeos/removable/*", "/mnt/chromeos/removable", current_drives);
    } else {
        // Search generic /media folder
        search_path("/media/*", "/media", current_drives);

        const char* username_cstr = nullptr;
        // First, try getting the username from the environment variable.
        if (const char* env_user = std::getenv("USER")) {
            username_cstr = env_user;

        }
        // As a fallback, use the POSIX-compliant getpwuid.
        else if (struct passwd* pw = getpwuid(getuid()))
        {
            username_cstr = pw->pw_name;
        }

        if (username_cstr) {
            std::string username(username_cstr);

            // Search /media/USERNAME/* folder
            std::string parent_media_path = "/media/" + username;
            search_path(parent_media_path + "/*", parent_media_path, current_drives);

            // Search /run/media/USERNAME/* folder
            std::string parent_run_path = "/run/media/" + username;
            search_path(parent_run_path + "/*", parent_run_path, current_drives);
        }
    }

    return current_drives;
}

} // namespace

RemovableDriveMonitorLinux::RemovableDriveMonitorLinux(Platform::IMainThreadDispatcher& dispatcher) :
    m_dispatcher(dispatcher)
{
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

void RemovableDriveMonitorLinux::update()
{
    std::unique_lock<std::mutex> inside_update_lock(m_inside_update_mutex, std::defer_lock);

    if (inside_update_lock.try_lock()) {
        // Got the lock without waiting. That means, the update was not running.
        // Run the update.
        std::vector<DriveData> current_drives = search_for_removable_drives();
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

boost::filesystem::path RemovableDriveMonitorLinux::get_path_on_removable_drive(const boost::filesystem::path& preferred_path) const
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

boost::filesystem::path RemovableDriveMonitorLinux::get_removable_drive_path_from_path(const boost::filesystem::path& path) const
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

size_t RemovableDriveMonitorLinux::removable_drives_count() const
{
    {
        std::scoped_lock<std::mutex> lock(m_drives_mutex);
        return m_current_drives.size();
    }
}

void RemovableDriveMonitorLinux::dispatch_status(const boost::filesystem::path& drive_path, RemovableDriveStatus status)
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
