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

#include "Slic3r/Log.hpp"
#include "Slic3r/Utils.hpp"

#include <dbus/dbus.h>

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

namespace {

// The document portal hands out paths of the form
// <mount point>/<doc id>/<file name>, with an extra by-app/<app id> in front
// when a file is opened on behalf of a specific application.
std::string document_id_from_path(const boost::filesystem::path& path)
{
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == nullptr) {
        return {};
    }

    auto it  = path.begin();
    auto end = path.end();
    for (const auto& part : boost::filesystem::path(runtime_dir) / "doc") {
        if (it == end || *it != part) {
            return {};
        }
        ++it;
    }

    if (it != end && *it == "by-app") {
        ++it; // by-app
        if (it != end) {
            ++it; // the application id
        }
    }

    // What follows is the document id, and after it the file name. A path that
    // ends at the document id is a directory, not a document.
    if (it == end) {
        return {};
    }
    const std::string document_id = it->string();
    return ++it == end ? std::string() : document_id;
}

// Ask the document portal which path a document stands for. Only GetHostPaths
// is callable from inside the sandbox; Info answers "Not allowed in sandbox".
boost::filesystem::path host_path_of_document(const std::string& document_id)
{
    DBusError error;
    dbus_error_init(&error);
    ScopeGuard error_guard([&error]() { dbus_error_free(&error); });

    DBusConnection* connection = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!connection) {
        SPDLOG_DEBUG("No session bus connection: {}", error.message ? error.message : "unknown");
        return {};
    }
    ScopeGuard connection_guard([connection]() { dbus_connection_unref(connection); });

    DBusMessage* message = dbus_message_new_method_call(
        "org.freedesktop.portal.Documents",  // Destination
        "/org/freedesktop/portal/documents", // Object path
        "org.freedesktop.portal.Documents",  // Interface
        "GetHostPaths"                       // Method
    );
    if (!message) {
        SPDLOG_ERROR("Failed to create the GetHostPaths message.");
        return {};
    }
    ScopeGuard message_guard([message]() { dbus_message_unref(message); });

    const char*     document_id_cstr = document_id.c_str();
    DBusMessageIter args;
    DBusMessageIter ids;
    dbus_message_iter_init_append(message, &args);
    if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &ids) ||
        !dbus_message_iter_append_basic(&ids, DBUS_TYPE_STRING, &document_id_cstr) ||
        !dbus_message_iter_close_container(&args, &ids)) {
        SPDLOG_ERROR("Ran out of memory while constructing the GetHostPaths message.");
        return {};
    }

    // The portal is a local service that answers in a few milliseconds, and
    // this runs on the main thread, so do not wait anywhere near as long as
    // the 25 seconds libdbus would default to.
    constexpr int timeout_ms = 1000;

    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(connection, message, timeout_ms, &error);
    if (!reply) {
        SPDLOG_DEBUG("GetHostPaths failed: {}", error.message ? error.message : "unknown error");
        return {};
    }
    ScopeGuard reply_guard([reply]() { dbus_message_unref(reply); });

    // The reply is a{say}: document id -> path as a byte array, since a path is
    // not required to be valid UTF-8. Only one id was asked for.
    DBusMessageIter reply_args;
    DBusMessageIter entries;
    if (!dbus_message_iter_init(reply, &reply_args) ||
        dbus_message_iter_get_arg_type(&reply_args) != DBUS_TYPE_ARRAY) {
        return {};
    }
    dbus_message_iter_recurse(&reply_args, &entries);
    if (dbus_message_iter_get_arg_type(&entries) != DBUS_TYPE_DICT_ENTRY) {
        return {};
    }

    DBusMessageIter entry;
    dbus_message_iter_recurse(&entries, &entry);
    dbus_message_iter_next(&entry); // Step over the key, on to the value.
    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_ARRAY) {
        return {};
    }

    DBusMessageIter bytes;
    dbus_message_iter_recurse(&entry, &bytes);
    const char* path_data  = nullptr;
    int         path_length = 0;
    dbus_message_iter_get_fixed_array(&bytes, &path_data, &path_length);
    if (path_data == nullptr || path_length <= 0) {
        return {};
    }

    return boost::filesystem::path(std::string(path_data, size_t(path_length)));
}

} // namespace

// A file dialog that goes through xdg-desktop-portal, which is what GTK does
// in a sandbox, reports the file under the document portal rather than where
// it really is. The file is the same one, but the path no longer says which
// drive it is on, so resolve it back before looking for a drive.
boost::filesystem::path RemovableDriveMonitorLinux::resolve_document_portal_path(
    const boost::filesystem::path& path
) const
{
    const std::string document_id = document_id_from_path(path);
    if (document_id.empty()) {
        return path;
    }

    {
        std::scoped_lock<std::mutex> lock(m_document_paths_mutex);
        if (const auto it = m_document_paths.find(document_id); it != m_document_paths.end()) {
            return it->second;
        }
    }

    boost::filesystem::path host_path = host_path_of_document(document_id);
    if (host_path.empty()) {
        return path;
    }

    SPDLOG_DEBUG("Document {} is {}", path.string(), host_path.string());
    {
        std::scoped_lock<std::mutex> lock(m_document_paths_mutex);
        m_document_paths.emplace(document_id, host_path);
    }
    return host_path;
}

boost::filesystem::path RemovableDriveMonitorLinux::get_removable_drive_path_from_path(const boost::filesystem::path& path) const
{
    const boost::filesystem::path real_path = resolve_document_portal_path(path);
    {
        std::scoped_lock<std::mutex> lock(m_drives_mutex);
        auto it_drive_data = find_drive(m_current_drives, real_path);
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
