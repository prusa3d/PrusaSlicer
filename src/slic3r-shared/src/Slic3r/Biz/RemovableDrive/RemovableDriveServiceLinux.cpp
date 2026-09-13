#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "RemovableDriveMonitorLinux.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include <fmt/format.h>
#include <boost/process.hpp>

#include "MountInfoLinux.hpp"

#include "Slic3r/Utils.hpp"

#include <dbus/dbus.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

namespace Slic3r::Biz::RemovableDrive {

RemovableDriveService::RemovableDriveService(Platform::IMainThreadDispatcher& dispatcher) :
    m_monitor(std::make_unique<RemovableDriveMonitorLinux>(dispatcher)),
    m_dispatcher(dispatcher)
{}

namespace {

// Look up the device node a path is mounted from, e.g. "/run/media/user/USB"
// -> "/dev/sdb1". Returns an empty string when the path is not a mount point.
std::string device_node_for_mount_point(const boost::filesystem::path& path)
{
    // /proc/self/mountinfo is used rather than /proc/mounts because its mount
    // point field is unambiguous and escaped in a documented way.
    std::ifstream mount_info("/proc/self/mountinfo");
    if (!mount_info) {
        return {};
    }

    // mountinfo holds the mount points the kernel resolved, so a path that
    // reaches here through a symlink would never match one of them.
    boost::system::error_code ec;
    const boost::filesystem::path canonical_path = boost::filesystem::canonical(path, ec);
    const std::string wanted = (ec ? path : canonical_path).string();

    std::string result;
    std::string line;
    while (std::getline(mount_info, line)) {
        const std::optional<MountInfoEntry> entry = parse_mount_info_line(line);
        if (entry && entry->mount_point == wanted) {
            // Keep scanning: where mounts are stacked, the last one is on top.
            result = entry->source;
        }
    }

    return result;
}

// Unmount through UDisks2 rather than the umount binary. This works without
// elevated privileges, is available inside sandboxes where umount is not, and
// lets the desktop show its usual "device can be safely removed" notification.
enum class EjectResult
{
    Ejected,     // UDisks2 unmounted the filesystem
    Busy,        // Something still has the filesystem open, worth another try
    Refused,     // UDisks2 answered and declined; umount would not do better
    Unsupported, // UDisks2 is not usable here, the caller should fall back
};

// UDisks2 names its block device objects after the device node, keeping
// alphanumeric characters and writing every other byte as an underscore
// followed by two hex digits, so /dev/dm-0 becomes .../block_devices/dm_2d0.
// Encrypted drives are unlocked as device mapper nodes, so this is not just a
// theoretical case, and an unencoded "-" is not even a valid object path.
std::string udisks2_block_device_path(const std::string& device_node)
{
    // /dev/mapper/<name> and /dev/disk/by-*/<name> are symlinks to the kernel
    // device node that UDisks2 names its objects after.
    boost::system::error_code    ec;
    const boost::filesystem::path resolved = boost::filesystem::canonical(device_node, ec);
    const std::string name = (ec ? boost::filesystem::path(device_node) : resolved).filename().string();

    std::string encoded;
    encoded.reserve(name.size());
    for (const unsigned char character : name) {
        const bool alphanumeric = (character >= '0' && character <= '9') ||
                                  (character >= 'A' && character <= 'Z') ||
                                  (character >= 'a' && character <= 'z');
        if (alphanumeric) {
            encoded += static_cast<char>(character);
        } else {
            encoded += fmt::format("_{:02x}", character);
        }
    }

    return "/org/freedesktop/UDisks2/block_devices/" + encoded;
}

EjectResult eject_via_udisks2(const boost::filesystem::path& path)
{
    const std::string device = device_node_for_mount_point(path);
    if (!device.starts_with("/dev/")) {
        SPDLOG_DEBUG("Could not resolve a device node for {}", path.string());
        return EjectResult::Unsupported;
    }

    DBusError error;
    dbus_error_init(&error);
    ScopeGuard error_guard([&error]() { dbus_error_free(&error); });

    DBusConnection* connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (!connection) {
        SPDLOG_DEBUG("No system bus connection: {}", error.message ? error.message : "unknown");
        return EjectResult::Unsupported;
    }
    ScopeGuard connection_guard([connection]() { dbus_connection_unref(connection); });

    const std::string object_path = udisks2_block_device_path(device);

    DBusMessage* message = dbus_message_new_method_call(
        "org.freedesktop.UDisks2",            // Destination
        object_path.c_str(),                  // Object path
        "org.freedesktop.UDisks2.Filesystem", // Interface
        "Unmount"                             // Method
    );
    if (!message) {
        SPDLOG_ERROR("Failed to create the UDisks2 Unmount message.");
        return EjectResult::Unsupported;
    }
    ScopeGuard message_guard([message]() { dbus_message_unref(message); });

    // Unmount takes a{sv} options; an empty dictionary selects the defaults.
    DBusMessageIter args;
    DBusMessageIter options;
    dbus_message_iter_init_append(message, &args);
    if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options) ||
        !dbus_message_iter_close_container(&args, &options)) {
        SPDLOG_ERROR("Ran out of memory while constructing the UDisks2 Unmount message.");
        return EjectResult::Unsupported;
    }

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, -1, &error);
    if (reply) {
        dbus_message_unref(reply);
        return EjectResult::Ejected;
    }

    // Distinguish "UDisks2 is not there" from "UDisks2 said no". Only the
    // former is worth retrying with umount.
    const std::string name = error.name ? error.name : "";
    const std::string text = error.message ? error.message : "unknown error";
    if (name == DBUS_ERROR_SERVICE_UNKNOWN || name == DBUS_ERROR_NAME_HAS_NO_OWNER ||
        name == DBUS_ERROR_UNKNOWN_OBJECT || name == DBUS_ERROR_UNKNOWN_INTERFACE ||
        name == DBUS_ERROR_UNKNOWN_METHOD) {
        SPDLOG_DEBUG("UDisks2 not usable for ejecting ({}): {}", name, text);
        return EjectResult::Unsupported;
    }

    if (name == "org.freedesktop.UDisks2.Error.DeviceBusy") {
        SPDLOG_DEBUG("The drive is still busy: {}", text);
        return EjectResult::Busy;
    }

    SPDLOG_ERROR("Ejecting failed: {}", text);
    return EjectResult::Refused;
}

// A file that has just been written can still be held open for a moment, which
// is exactly the situation the eject button is pressed in: the export finished
// a second ago. The unmount then fails although nothing is wrong with it, so
// give the drive a little time instead of reporting a failure straight away.
EjectResult eject_via_udisks2_with_retries(const boost::filesystem::path& path)
{
    constexpr int  attempts = 4;
    constexpr auto delay    = std::chrono::milliseconds(300);

    for (int attempt = 1;; ++attempt) {
        const EjectResult result = eject_via_udisks2(path);
        if (result != EjectResult::Busy) {
            return result;
        }
        if (attempt == attempts) {
            SPDLOG_ERROR("Ejecting failed: the drive is still busy.");
            return EjectResult::Refused;
        }
        std::this_thread::sleep_for(delay);
    }
}

bool eject_inner(const boost::filesystem::path& path)
{
    switch (eject_via_udisks2_with_retries(path)) {
    case EjectResult::Ejected:     return true;
    case EjectResult::Busy:        return false; // Not returned, the retries end in Refused.
    case EjectResult::Refused:     return false;
    case EjectResult::Unsupported: break;        // Fall through to umount.
    }

    // there is no usable command in c++ so terminal command is used instead
    // but neither triggers "succesful safe removal messege"
    boost::process::ipstream istd_err;
    boost::process::child child(boost::process::search_path("umount"), path.string().c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
    std::string line;
    while (child.running() && std::getline(istd_err, line)) {
    }
    // wait for command to finish
    std::error_code ec;
    child.wait(ec);
    if (ec) {
        // The wait call can fail, as it did in https://github.com/prusa3d/PrusaSlicer/issues/5507
        // It can happen even in cases where the eject is sucessful, but better report it as failed.
        // We did not find a way to reliably retrieve the exit code of the process.
        SPDLOG_ERROR("boost::process::child::wait() failed during Ejection. State of Ejection is unknown. Error code: {}", ec.value());
        return false;
    } else if (int err = child.exit_code(); err) {
        SPDLOG_ERROR("Ejecting failed. Exit code: {}", std::to_string(err));
        return false;
    }
    return true;
}
} // namespace

void RemovableDriveService::eject_in_thread(const boost::filesystem::path& path)
{
    if (m_eject_thread.joinable()) {
        m_eject_thread.request_stop();
        m_eject_thread.join();
    }

    m_eject_thread = JThread::JThread(
        [this, path](JThread::StopToken stop_token)
        {
            bool res = eject_inner(path);
            if (!res) {
                dispatch_status_on_main_thread(path, RemovableDriveStatus::Failed);
            }
            // TODO: Dispatch Removed?
        }
    );
}

} // namespace Slic3r::Biz::RemovableDrive
